/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "pdc.h"
#include <gio/gio.h>
#include <glib-unix.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <libqmi-glib.h>
#include <libqrtr-glib.h>
#include "mcfg.h"
#include "imsd.h"

#define LIST_CONFIGS_TIMEOUT_SECS 2

/*
 *  (biktor) NOTES:
 * We need to do a couple of things here. This is just
 * a placeholder of qmicli's functionality to retrieve
 * profiles.
 * I need to first find a way to retrieve the MCC/MNC
 * fields via the PDC service. They are there in mcfg_sw
 * files, but libqmi doesn't seem to have anything implemented
 * to read it. Maybe I can download them and check them offline
 * or I can use some command to match against the IDs from the NAS
 *
 * If no embedded profile is found I should look for them in the
 * vendor partition, as it contains a ton more profiles we can use
 *
 * If everything else fails, I should use the MFS to set (as little)
 * nvitems as possible to set things up so the DCM client in the modem
 * can request an activation, but I'm don't know how to do that yet.
 *
 * We need to have a functional test setup to set arbitrary IMS configuration
 * in open5gs / kamailio to test that scenario, as all the commercial SIMs
 * I have seem to work with ROW_COMMERCIAL directly.
 * I could overwrite that or delete it, but not sure I can really go back
 * afterwards in my test target as its secfused and I don't want to kill
 * the efs because of a silly test.
 *
 * We also need to handle multiple sims here, as one SIM may use a specific one
 * and the other one a different profile (i.e. att in one slot and vzw in the
 * other) Need to see how the service handles this. Also eSIMs, which will
 * probably be affected by this
 *
 * (For whoever may read this besides me)
 * There are two types of persistent device configs as far as I know
 * MCFG_HW -> Handles physical config of the device
 * MCFG_SW -> handles carrier configuration for the device
 * Anything from the IMS support to emergency carrier config, AGPS
 * and even brightness control on some phones relies on mcfg_sw
 * For our use case here we only need to worry about mcfg_sw. Typically
 * mcfg_hw has only one available config anyway and is already set
 *
 * On the sw side of things, it gets messy very fast. There are carrier
 * based configs, with imsi-prefix based overlays on top for some carriers,
 * and currently either the service or libqmi doesn't provide a way to
 * easily check which mcfg_sw file to use unless you already know what
 * you are looking for.
 *
 */

/* Context */
typedef struct {
  QmiDevice *device;
  QmiClientPdc *client;
  GCancellable *cancellable;

  /* local data */
  guint timeout_id;
  GArray *config_list;
  guint configs_loaded;
  GArray *active_config_id;
  GArray *pending_config_id;
  gboolean ids_loaded;
  guint list_configs_indication_id;
  guint get_selected_config_indication_id;

  guint load_config_indication_id;
  guint get_config_info_indication_id;

  guint set_selected_config_indication_id;
  guint activate_config_indication_id;

  guint refresh_indication_id;

  guint token;

} Context;

static Context *ctx;

/* Activate config: Begin */
static void activate_config_ready(QmiClientPdc *client, GAsyncResult *res) {
  g_autoptr(GError) error = NULL;
  g_autoptr(QmiMessagePdcActivateConfigOutput) output = NULL;

  output = qmi_client_pdc_activate_config_finish(client, res, &error);
  if (!output) {
    g_printerr("error: operation failed: %s\n", error->message);
    return;
  }

  if (!qmi_message_pdc_activate_config_output_get_result(output, &error)) {
    g_printerr("error: couldn't activate config: %s\n", error->message);
    return;
  }
}

static void set_selected_config_ready(QmiClientPdc *client, GAsyncResult *res) {
  g_autoptr(GError) error = NULL;
  g_autoptr(QmiMessagePdcSetSelectedConfigOutput) output = NULL;
  g_autoptr(QmiMessagePdcActivateConfigInput) input = NULL;

  output = qmi_client_pdc_set_selected_config_finish(client, res, &error);
  if (!output) {
    g_printerr("error: operation failed: %s\n", error->message);
    return;
  }
  input = qmi_message_pdc_activate_config_input_new();
  if (!qmi_message_pdc_activate_config_input_set_config_type(input, 0x01,
                                                             &error) ||
      !qmi_message_pdc_activate_config_input_set_token(input, ctx->token++,
                                                       &error)) {
    g_printerr("error: couldn't create input data bundle: '%s'\n",
               error->message);
    return;
  }
  qmi_client_pdc_activate_config(ctx->client, input, 10, ctx->cancellable,
                                 (GAsyncReadyCallback)activate_config_ready,
                                 NULL);
}

static void set_selected_config(ConfigInfo *this_config) {
  g_autoptr(QmiMessagePdcSetSelectedConfigInput) input = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GArray) id = NULL;

  input = qmi_message_pdc_set_selected_config_input_new();
  if (!qmi_message_pdc_set_selected_config_input_set_type_with_id_v2(
          input, 0x01, this_config->id, &error) ||
      !qmi_message_pdc_set_selected_config_input_set_token(input, ctx->token++,
                                                           &error)) {
    g_printerr("error: couldn't create input data bundle: '%s'\n",
               error->message);
    return;
  }

  qmi_client_pdc_set_selected_config(
      ctx->client, input, 10, ctx->cancellable,
      (GAsyncReadyCallback)set_selected_config_ready, NULL);
}

/* Activate config: END */
/* GET ALL PROFILES: Begin */

static const char *status_string(GArray *id) {
  if (!id)
    return "Unknown";
  if (ctx->active_config_id && id->len == ctx->active_config_id->len &&
      memcmp(id->data, ctx->active_config_id->data, id->len) == 0)
    return "Active  ";
  if (ctx->pending_config_id && id->len == ctx->pending_config_id->len &&
      memcmp(id->data, ctx->pending_config_id->data, id->len) == 0)
    return "Pending";
  return "Inactive";
}

static void print_configs(GArray *configs) {
  guint i;
  g_printf("Total configurations: %u\n", ctx->config_list->len);
  g_print("Status   \t Version\t ID \t\t\t\t\t\t\t\t Name \n");
  for (i = 0; i < ctx->config_list->len; i++) {
    ConfigInfo *current_config;

    current_config = &g_array_index(ctx->config_list, ConfigInfo, i);
    g_print("%s", status_string(current_config->id));
    g_print("\t0x%X", current_config->version);
    g_print("\t");
    for (int i = 0; i < current_config->id->len; i++) {
      g_print("%02x%s", g_array_index(current_config->id, guint8, i),
              ((i + 1) < current_config->id->len) ? ":" : "");
    }
    g_print("\t%s\n", current_config->description);
    if (strcmp(current_config->description, "ROW_Commercial") == 0) {
      set_selected_config(current_config);
    }
  }
}

static void check_list_config_completed(void) {
  if (ctx->configs_loaded == ctx->config_list->len && ctx->ids_loaded) {
    print_configs(ctx->config_list);
  }
}

static void get_config_info_ready(QmiClientPdc *client, GAsyncResult *res) {
  g_autoptr(GError) error = NULL;
  g_autoptr(QmiMessagePdcGetConfigInfoOutput) output = NULL;

  output = qmi_client_pdc_get_config_info_finish(client, res, &error);
  if (!output) {
    g_printerr("error: operation failed: %s\n", error->message);
    return;
  }

  if (!qmi_message_pdc_get_config_info_output_get_result(output, &error)) {
    g_printerr("error: couldn't get config info: %s\n", error->message);
    return;
  }
}

static void
get_config_info_ready_indication(QmiClientPdc *client,
                                 QmiIndicationPdcGetConfigInfoOutput *output) {
  g_autoptr(GError) error = NULL;
  ConfigInfo *current_config = NULL;
  guint32 token;
  const gchar *description;
  guint i;
  guint16 error_code = 0;

  if (!qmi_indication_pdc_get_config_info_output_get_indication_result(
          output, &error_code, &error)) {
    g_printerr("error: couldn't get config info: %s\n", error->message);
    return;
  }

  if (error_code != 0) {
    g_printerr("error: couldn't get config info: %s\n",
               qmi_protocol_error_get_string((QmiProtocolError)error_code));
    return;
  }

  if (!qmi_indication_pdc_get_config_info_output_get_token(output, &token,
                                                           &error)) {
    g_printerr("error: couldn't get config info token: %s\n", error->message);
    return;
  }

  /* Look for the current config in the list */
  for (i = 0; i < ctx->config_list->len; i++) {
    current_config = &g_array_index(ctx->config_list, ConfigInfo, i);
    if (current_config->token == token)
      break;
  }

  /* Store total size, ve rsion and description of the current config */
  if (!qmi_indication_pdc_get_config_info_output_get_total_size(
          output, &current_config->total_size, &error) ||
      !qmi_indication_pdc_get_config_info_output_get_version(
          output, &current_config->version, &error) ||
      !qmi_indication_pdc_get_config_info_output_get_description(
          output, &description, &error)) {
    g_printerr("error: couldn't get config info details: %s\n", error->message);
    return;
  }
  current_config->description = g_strdup(description);

  ctx->configs_loaded++;

  check_list_config_completed();
}

static gboolean list_configs_timeout(void) {
  if (!ctx->config_list)
    g_printf(
        "[PDC] Timeout when attempting to get persistent device configs!\n");
  return FALSE;
}

static void
list_configs_ready_indication(QmiClientPdc *client,
                              QmiIndicationPdcListConfigsOutput *output) {
  g_autoptr(GError) error = NULL;
  GArray *configs = NULL;
  guint i;
  guint16 error_code = 0;

  /* Remove timeout as soon as we get the indication */
  if (ctx->timeout_id) {
    g_source_remove(ctx->timeout_id);
    ctx->timeout_id = 0;
  }

  if (!qmi_indication_pdc_list_configs_output_get_indication_result(
          output, &error_code, &error)) {
    g_printerr("error: couldn't list configs: %s\n", error->message);
    return;
  }

  if (error_code != 0) {
    g_printerr("error: couldn't list config: %s\n",
               qmi_protocol_error_get_string((QmiProtocolError)error_code));
    return;
  }

  if (!qmi_indication_pdc_list_configs_output_get_configs(output, &configs,
                                                          &error)) {
    g_printerr("error: couldn't list configs: %s\n", error->message);
    return;
  }

  /* Preallocate config list and request details for each */
  ctx->config_list =
      g_array_sized_new(FALSE, TRUE, sizeof(ConfigInfo), configs->len);
  g_array_set_size(ctx->config_list, configs->len);

  for (i = 0; i < configs->len; i++) {
    ConfigInfo *current_info;
    QmiIndicationPdcListConfigsOutputConfigsElement *element;
    guint32 token;
    g_autoptr(QmiMessagePdcGetConfigInfoInput) input = NULL;

    token = ctx->token++;

    element = &g_array_index(
        configs, QmiIndicationPdcListConfigsOutputConfigsElement, i);

    current_info = &g_array_index(ctx->config_list, ConfigInfo, i);
    current_info->token = token;
    current_info->id = g_array_ref(element->id);
    current_info->config_type = element->config_type;

    input = qmi_message_pdc_get_config_info_input_new();

    /* Add type with id */
    if (!qmi_message_pdc_get_config_info_input_set_type_with_id_v2(
            input, element->config_type, current_info->id, &error)) {
      g_printerr("error: couldn't set type with id: %s\n", error->message);
      return;
    }

    /* Add token */
    if (!qmi_message_pdc_get_config_info_input_set_token(input, token,
                                                         &error)) {
      g_printerr("error: couldn't set token: %s\n", error->message);
      return;
    }

    qmi_client_pdc_get_config_info(ctx->client, input, 10, ctx->cancellable,
                                   (GAsyncReadyCallback)get_config_info_ready,
                                   NULL);
  }

  check_list_config_completed();
}

static void list_configs_ready(QmiClientPdc *client, GAsyncResult *res) {
  g_autoptr(GError) error = NULL;
  g_autoptr(QmiMessagePdcListConfigsOutput) output = NULL;

  output = qmi_client_pdc_list_configs_finish(client, res, &error);
  if (!output) {
    g_printerr("error: operation failed: %s\n", error->message);
    return;
  }

  if (!qmi_message_pdc_list_configs_output_get_result(output, &error)) {
    g_printerr("error: couldn't list configs: %s\n", error->message);
    return;
  }
}

static void get_selected_config_ready_indication(
    QmiClientPdc *client, QmiIndicationPdcGetSelectedConfigOutput *output) {
  g_autoptr(GError) error = NULL;
  GArray *pending_id = NULL;
  GArray *active_id = NULL;
  guint16 error_code = 0;

  if (!qmi_indication_pdc_get_selected_config_output_get_indication_result(
          output, &error_code, &error)) {
    g_printerr("error: couldn't get selected config: %s\n", error->message);
    g_print("%s: Failed!\n", __func__);
    return;
  }

  if (error_code != 0 &&
      error_code !=
          QMI_PROTOCOL_ERROR_NOT_PROVISIONED) { /* No configs active */
    g_printerr("error: couldn't get selected config: %s\n",
               qmi_protocol_error_get_string((QmiProtocolError)error_code));
    g_print("%s: Failed!\n", __func__);
    return;
  }

  qmi_indication_pdc_get_selected_config_output_get_pending_id(
      output, &pending_id, NULL);
  qmi_indication_pdc_get_selected_config_output_get_active_id(output,
                                                              &active_id, NULL);
  if (active_id)
    ctx->active_config_id = g_array_ref(active_id);
  if (pending_id)
    ctx->pending_config_id = g_array_ref(pending_id);

  ctx->ids_loaded = TRUE;

  check_list_config_completed();
}

static void get_selected_config_ready(QmiClientPdc *client, GAsyncResult *res) {
  g_autoptr(GError) error = NULL;
  g_autoptr(QmiMessagePdcGetSelectedConfigOutput) output = NULL;

  output = qmi_client_pdc_get_selected_config_finish(client, res, &error);
  if (!qmi_message_pdc_get_selected_config_output_get_result(output, &error)) {
    g_printerr("error: couldn't get selected config: %s\n", error->message);
    return;
  }
}

static void run_list_configs() {
  g_autoptr(QmiMessagePdcListConfigsInput) input = NULL;
  g_autoptr(QmiMessagePdcGetSelectedConfigInput) get_selected_config_input =
      NULL;
  g_autoptr(GError) error = NULL;
  QmiPdcConfigurationType config_type;
  config_type = 0x01; // We only care about sotware

  g_print("[PDC] Attempting to retrieve on device configs...");

  /* Results are reported via indications */
  ctx->list_configs_indication_id =
      g_signal_connect(ctx->client, "list-configs",
                       G_CALLBACK(list_configs_ready_indication), NULL);
  ctx->get_selected_config_indication_id =
      g_signal_connect(ctx->client, "get-selected-config",
                       G_CALLBACK(get_selected_config_ready_indication), NULL);
  ctx->get_config_info_indication_id =
      g_signal_connect(ctx->client, "get-config-info",
                       G_CALLBACK(get_config_info_ready_indication), NULL);

  input = qmi_message_pdc_list_configs_input_new();
  if (!qmi_message_pdc_list_configs_input_set_config_type(input, config_type,
                                                          &error) ||
      !qmi_message_pdc_list_configs_input_set_token(input, ctx->token++,
                                                    &error)) {
    g_printerr("error: couldn't create input data bundle: '%s'\n",
               error->message);
    return;
  }

  get_selected_config_input = qmi_message_pdc_get_selected_config_input_new();
  if (!qmi_message_pdc_get_selected_config_input_set_config_type(
          get_selected_config_input, config_type, &error) ||
      !qmi_message_pdc_get_selected_config_input_set_token(
          get_selected_config_input, ctx->token++, &error)) {
    g_printerr("error: couldn't create input data bundle: '%s'\n",
               error->message);
    return;
  }

  /* We need a timeout, because there will be no indications if no configs
   * are loaded */
  ctx->timeout_id = g_timeout_add_seconds(
      LIST_CONFIGS_TIMEOUT_SECS, (GSourceFunc)list_configs_timeout, NULL);

  qmi_client_pdc_list_configs(ctx->client, input, 10, ctx->cancellable,
                              (GAsyncReadyCallback)list_configs_ready, NULL);

  qmi_client_pdc_get_selected_config(
      ctx->client, get_selected_config_input, 10, ctx->cancellable,
      (GAsyncReadyCallback)get_selected_config_ready, NULL);
}

void pdc_start(QmiDevice *device, QmiClientPdc *client,
               GCancellable *cancellable) {

  /* Initialize context */
  ctx = g_slice_new(Context);
  ctx->device = g_object_ref(device);
  ctx->client = g_object_ref(client);
  ctx->cancellable = g_object_ref(cancellable);
  scan_pdc_mcfgs(PDC_FOLDER_PATH);
  run_list_configs();
}