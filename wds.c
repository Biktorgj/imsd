/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Copyright (c) 2024, Biktorgj
 */
#include "wds.h"
#include "dcm.h"
#include "qmi-ims-client.h"
#include <arpa/inet.h>
#include <gio/gio.h>
#include <glib-unix.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <libqmi-glib.h>
#include <libqrtr-glib.h>
#include <netinet/in.h>
#define VALIDATE_MASK_NONE(str) (str ? str : "none")
#define QMI_WDS_IP_FAMILY_IPV4 4

/* Context */
typedef struct {
  QmiDevice *device;
  QmiClientWds *client;
  GCancellable *cancellable;
  guint8 profile_id;
  gulong network_started_id;
  guint packet_status_timeout_id;
  // Handler to start and stop network
  guint32 packet_data_handle;
  // WDS network bringup step
  guint8 connection_readiness_step;
  // Mux ID and device in netlink
  // We should clean this up when we quit or we crash
  // **without** interfering with ModemManager...

  // Current IP Address
  guint8 ip_addr_type;
  uint8_t ip_address[128];

  gchar *link_name;
  guint mux_id;
  guint setup_link_done;
  // Endpoint
  guint endpoint_type;
  guint endpoint_ifnum;

} Context;
static Context *ctx;

/********** PROFILES ************/

typedef struct {
  guint i;
  GArray *profile_list;
} GetProfileListContext;

guint8 wds_get_readiness_step() { return ctx->connection_readiness_step; }

guint32 wds_get_packet_handle() { return ctx->packet_data_handle; }

guint8 wds_get_profile_id() { return ctx->profile_id; }

guint8 wds_get_mux_id() { return ctx->mux_id; }

void wds_copy_ip_address(uint8_t *ip_address) {
  g_print("%s: CURR IP ADDR: %s\n", __func__, ctx->ip_address);
  strncpy((char *)ip_address, (char *)ctx->ip_address, strlen((char *)ctx->ip_address));
}

static void get_next_profile_settings(GetProfileListContext *inner_ctx);

static void get_profile_settings_ready(QmiClientWds *client, GAsyncResult *res,
                                       GetProfileListContext *inner_ctx) {
  QmiMessageWdsGetProfileSettingsOutput *output;
  QmiMessageWdsGetProfileListOutputProfileListProfile *profile;

  profile = &g_array_index(inner_ctx->profile_list,
                           QmiMessageWdsGetProfileListOutputProfileListProfile,
                           inner_ctx->i);
  GError *error = NULL;
  g_printerr(" -> Profile %i\n", profile->profile_index);

  output = qmi_client_wds_get_profile_settings_finish(client, res, &error);
  if (!output) {
    g_printerr("error: operation failed: %s\n", error->message);
    g_error_free(error);
  } else if (!qmi_message_wds_get_profile_settings_output_get_result(output,
                                                                     &error)) {
    QmiWdsDsProfileError ds_profile_error;

    if (g_error_matches(error, QMI_PROTOCOL_ERROR,
                        QMI_PROTOCOL_ERROR_EXTENDED_INTERNAL) &&
        qmi_message_wds_get_profile_settings_output_get_extended_error_code(
            output, &ds_profile_error, NULL)) {
      g_printerr("error: couldn't get profile settings: ds profile error: %s\n",
                 qmi_wds_ds_profile_error_get_string(ds_profile_error));
    } else {
      g_printerr("error: couldn't get profile settings: %s\n", error->message);
    }
    g_error_free(error);
    qmi_message_wds_get_profile_settings_output_unref(output);
  } else {
    const gchar *str;
    guint8 context_number;
    QmiWdsPdpType pdp_type;
    QmiWdsAuthentication auth;
    QmiWdsApnTypeMask apn_type;
    gboolean flag;

    if (qmi_message_wds_get_profile_settings_output_get_apn_name(output, &str,
                                                                 NULL))
      g_print("  -->APN: '%s'\n", str);
    if (qmi_message_wds_get_profile_settings_output_get_apn_type_mask(
            output, &apn_type, NULL)) {
      g_autofree gchar *aux = NULL;

      aux = qmi_wds_apn_type_mask_build_string_from_mask(apn_type);
      g_print("  -->APN type: '%s'\n", VALIDATE_MASK_NONE(aux));
      if (apn_type == APN_TYPE_MASK_IMS) {
        g_printerr("    --> We found a valid APN type: %i \n", profile->profile_index);
        ctx->profile_id = profile->profile_index;
        ctx->connection_readiness_step = WDS_CONNECTION_STATE_PROFILE_READY;
      }
    }
    if (qmi_message_wds_get_profile_settings_output_get_pdp_type(
            output, &pdp_type, NULL))
      g_print("  -->PDP type: '%s'\n", qmi_wds_pdp_type_get_string(pdp_type));
    if (qmi_message_wds_get_profile_settings_output_get_pdp_context_number(
            output, &context_number, NULL))
      g_print("  -->PDP context number: '%d'\n", context_number);
    if (qmi_message_wds_get_profile_settings_output_get_username(output, &str,
                                                                 NULL))
      g_print("  -->Username: '%s'\n", str);
    if (qmi_message_wds_get_profile_settings_output_get_password(output, &str,
                                                                 NULL))
      g_print("  -->Password: '%s'\n", str);
    if (qmi_message_wds_get_profile_settings_output_get_authentication(
            output, &auth, NULL)) {
      g_autofree gchar *aux = NULL;

      aux = qmi_wds_authentication_build_string_from_mask(auth);
      g_print("  -->Auth: '%s'\n", VALIDATE_MASK_NONE(aux));
    }
    if (qmi_message_wds_get_profile_settings_output_get_roaming_disallowed_flag(
            output, &flag, NULL))
      g_print("  -->No roaming: '%s'\n", flag ? "yes" : "no");
    if (qmi_message_wds_get_profile_settings_output_get_apn_disabled_flag(
            output, &flag, NULL))
      g_print("  -->APN disabled: '%s'\n", flag ? "yes" : "no");
    qmi_message_wds_get_profile_settings_output_unref(output);
  }

  /* Keep on */
  inner_ctx->i++;
  get_next_profile_settings(inner_ctx);
}

static void get_next_profile_settings(GetProfileListContext *inner_ctx) {
  QmiMessageWdsGetProfileListOutputProfileListProfile *profile;
  QmiMessageWdsGetProfileSettingsInput *input;

  if (inner_ctx->i >= inner_ctx->profile_list->len) {
    /* All done */
    g_print("We reached the end of the profiles\n");
    if (ctx->profile_id == 0) {
      ctx->connection_readiness_step = WDS_CONNECTION_FIND_PROFILE_ADD_MODIFY;
    }
    g_array_unref(inner_ctx->profile_list);
    g_slice_free(GetProfileListContext, inner_ctx);
    return;
  }

  profile = &g_array_index(inner_ctx->profile_list,
                           QmiMessageWdsGetProfileListOutputProfileListProfile,
                           inner_ctx->i);
  g_print(" [%u] %s - %s\n", profile->profile_index,
          qmi_wds_profile_type_get_string(profile->profile_type),
          profile->profile_name);

  input = qmi_message_wds_get_profile_settings_input_new();
  qmi_message_wds_get_profile_settings_input_set_profile_id(
      input, profile->profile_type, profile->profile_index, NULL);
  qmi_client_wds_get_profile_settings(
      ctx->client, input, 3, NULL,
      (GAsyncReadyCallback)get_profile_settings_ready, inner_ctx);
  qmi_message_wds_get_profile_settings_input_unref(input);
}

static void get_profile_list_ready(QmiClientWds *client, GAsyncResult *res) {
  GError *error = NULL;
  QmiMessageWdsGetProfileListOutput *output;
  GetProfileListContext *inner_ctx;
  GArray *profile_list = NULL;

  output = qmi_client_wds_get_profile_list_finish(client, res, &error);
  if (!output) {
    g_printerr("error: operation failed: %s\n", error->message);
    g_error_free(error);
    return;
  }
  if (!qmi_message_wds_get_profile_list_output_get_result(output, &error)) {
    QmiWdsDsProfileError ds_profile_error;

    if (g_error_matches(error, QMI_PROTOCOL_ERROR,
                        QMI_PROTOCOL_ERROR_EXTENDED_INTERNAL) &&
        qmi_message_wds_get_profile_list_output_get_extended_error_code(
            output, &ds_profile_error, NULL)) {
      g_printerr("error: couldn't get profile list: ds profile error: %s\n",
                 qmi_wds_ds_profile_error_get_string(ds_profile_error));
    } else {
      g_printerr("error: couldn't get profile list: %s\n", error->message);
    }

    g_error_free(error);
    qmi_message_wds_get_profile_list_output_unref(output);
    return;
  }

  qmi_message_wds_get_profile_list_output_get_profile_list(output,
                                                           &profile_list, NULL);

  if (!profile_list || !profile_list->len) {
    g_printerr("Profile list empty\n");
    ctx->connection_readiness_step = WDS_CONNECTION_FIND_PROFILE_ADD_MODIFY;
    qmi_message_wds_get_profile_list_output_unref(output);
    return;
  }

  g_debug("Profile list retrieved:\n");

  inner_ctx = g_slice_new(GetProfileListContext);
  inner_ctx->profile_list = g_array_ref(profile_list);
  inner_ctx->i = 0;
  get_next_profile_settings(inner_ctx);
}

void get_profile_list(gchar *get_profile_list_str) {
  QmiMessageWdsGetProfileListInput *input;
  input = qmi_message_wds_get_profile_list_input_new();
  if (g_str_equal(get_profile_list_str, "3gpp"))
    qmi_message_wds_get_profile_list_input_set_profile_type(
        input, QMI_WDS_PROFILE_TYPE_3GPP, NULL);
  else if (g_str_equal(get_profile_list_str, "3gpp2"))
    qmi_message_wds_get_profile_list_input_set_profile_type(
        input, QMI_WDS_PROFILE_TYPE_3GPP2, NULL);
  else {
    g_printerr(
        "error: invalid profile type '%s'. Expected '3gpp' or '3gpp2'.'\n",
        get_profile_list_str);
    return;
  }

  g_print("[WDS] Retrieving profile list...\n");
  qmi_client_wds_get_profile_list(ctx->client, input, 10, ctx->cancellable,
                                  (GAsyncReadyCallback)get_profile_list_ready,
                                  NULL);
  qmi_message_wds_get_profile_list_input_unref(input);
  return;
}
/************ PROFILES *************/

static void get_packet_service_status_ready(QmiClientWds *client,
                                            GAsyncResult *res) {
  GError *error = NULL;
  QmiMessageWdsGetPacketServiceStatusOutput *output;
  QmiWdsConnectionStatus status;

  output = qmi_client_wds_get_packet_service_status_finish(client, res, &error);
  if (!output) {
    g_printerr("error: operation failed: %s\n", error->message);
    g_error_free(error);
    return;
  }

  if (!qmi_message_wds_get_packet_service_status_output_get_result(output,
                                                                   &error)) {
    g_printerr("error: couldn't get packet service status: %s\n",
               error->message);
    g_error_free(error);
    qmi_message_wds_get_packet_service_status_output_unref(output);
    return;
  }

  qmi_message_wds_get_packet_service_status_output_get_connection_status(
      output, &status, NULL);

  g_debug("[%s] Connection status: '%s'\n",
          qmi_device_get_path_display(ctx->device),
          qmi_wds_connection_status_get_string(status));

  qmi_message_wds_get_packet_service_status_output_unref(output);
}

gboolean get_pkt_svc_status() {
  g_printerr("Asynchronously getting packet service status...\n");
  qmi_client_wds_get_packet_service_status(
      ctx->client, NULL, 10, ctx->cancellable,
      (GAsyncReadyCallback)get_packet_service_status_ready, NULL);
  return FALSE;
}

static void get_autoconnect_settings_ready(QmiClientWds *client,
                                           GAsyncResult *res) {
  QmiMessageWdsGetAutoconnectSettingsOutput *output;
  GError *error = NULL;
  QmiWdsAutoconnectSetting status;
  QmiWdsAutoconnectSettingRoaming roaming;

  output = qmi_client_wds_get_autoconnect_settings_finish(client, res, &error);
  if (!output) {
    g_printerr("error: operation failed: %s\n", error->message);
    g_error_free(error);
    return;

  }

  if (!qmi_message_wds_get_autoconnect_settings_output_get_result(output,
                                                                  &error)) {
    g_printerr("error: couldn't get autoconnect settings: %s\n",
               error->message);
    g_error_free(error);
    qmi_message_wds_get_autoconnect_settings_output_unref(output);
    return;
  }

  g_print("Autoconnect settings retrieved:\n");
  qmi_message_wds_get_autoconnect_settings_output_get_status(output, &status,
                                                             NULL);
  g_print(" Status: '%s'\n", qmi_wds_autoconnect_setting_get_string(status));

  if (qmi_message_wds_get_autoconnect_settings_output_get_roaming(
          output, &roaming, NULL))
    g_print(" Roaming: '%s'\n",
            qmi_wds_autoconnect_setting_roaming_get_string(roaming));

  qmi_message_wds_get_autoconnect_settings_output_unref(output);
}

void get_autoconnect_settings() {
  qmi_client_wds_get_autoconnect_settings(
      ctx->client, NULL, 10, ctx->cancellable,
      (GAsyncReadyCallback)get_autoconnect_settings_ready, NULL);
}

/*
 * STOP / START NETWORK
 *
 *
 *
 *
 *
 */

static void start_network_ready(QmiClientWds *client, GAsyncResult *res) {
  GError *error = NULL;
  QmiMessageWdsStartNetworkOutput *output;
  g_print("[WDS] Start network READY\n");
  output = qmi_client_wds_start_network_finish(client, res, &error);
  if (!output) {
    g_printerr("error: operation failed: %s\n", error->message);
    g_error_free(error);
    ctx->connection_readiness_step--;
    return;
  }

  if (!qmi_message_wds_start_network_output_get_result(output, &error)) {
    g_printerr("error: couldn't start network: %s\n", error->message);
    if (g_error_matches(error, QMI_PROTOCOL_ERROR,
                        QMI_PROTOCOL_ERROR_CALL_FAILED)) {
      QmiWdsCallEndReason cer;
      QmiWdsVerboseCallEndReasonType verbose_cer_type;
      gint16 verbose_cer_reason;

      if (qmi_message_wds_start_network_output_get_call_end_reason(output, &cer,
                                                                   NULL))
        g_printerr("call end reason (%u): %s\n", cer,
                   qmi_wds_call_end_reason_get_string(cer));

      if (qmi_message_wds_start_network_output_get_verbose_call_end_reason(
              output, &verbose_cer_type, &verbose_cer_reason, NULL))
        g_printerr(
            "verbose call end reason (%u,%d): [%s] %s\n", verbose_cer_type,
            verbose_cer_reason,
            qmi_wds_verbose_call_end_reason_type_get_string(verbose_cer_type),
            qmi_wds_verbose_call_end_reason_get_string(verbose_cer_type,
                                                       verbose_cer_reason));
            ctx->connection_readiness_step = 5;

    }

    g_error_free(error);
    qmi_message_wds_start_network_output_unref(output);
    ctx->connection_readiness_step++;
    return;
  }

  qmi_message_wds_start_network_output_get_packet_data_handle(
      output, &ctx->packet_data_handle, NULL);
  qmi_message_wds_start_network_output_unref(output);

  g_print("[WDS][%s] Network started, handle: '%u'\n",
          qmi_device_get_path_display(ctx->device),
          (guint)ctx->packet_data_handle);
  ctx->connection_readiness_step++;
}

static void stop_network_ready(QmiClientWds *client, GAsyncResult *res) {
  GError *error = NULL;
  QmiMessageWdsStopNetworkOutput *output;

  output = qmi_client_wds_stop_network_finish(client, res, &error);
  if (!output) {
    g_printerr("error: operation failed: %s\n", error->message);
    g_error_free(error);
    return;
  }

  if (!qmi_message_wds_stop_network_output_get_result(output, &error)) {
    g_printerr("error: couldn't stop network: %s\n", error->message);
    g_error_free(error);
    qmi_message_wds_stop_network_output_unref(output);
    return;
  }

  g_print("[%s] Network stopped\n", qmi_device_get_path_display(ctx->device));
  qmi_message_wds_stop_network_output_unref(output);
}

void wds_do_stop_network(gboolean disable_autoconnect) {
  QmiMessageWdsStopNetworkInput *input;
  g_info("*** STOP NETWORK!!!\n");
  input = qmi_message_wds_stop_network_input_new();
  qmi_message_wds_stop_network_input_set_packet_data_handle(
      input, ctx->packet_data_handle, NULL);
  if (disable_autoconnect)
    qmi_message_wds_stop_network_input_set_disable_autoconnect(input, TRUE,
                                                               NULL);

  g_print("Network cancelled... releasing resources\n");
  qmi_client_wds_stop_network(ctx->client, input, 120, ctx->cancellable,
                              (GAsyncReadyCallback)stop_network_ready, NULL);
  qmi_message_wds_stop_network_input_unref(input);
}

void wds_start_network() {
  QmiMessageWdsStartNetworkInput *input = NULL;
  g_print("[WDS] Starting network!\n");
  input = qmi_message_wds_start_network_input_new();
  qmi_message_wds_start_network_input_set_apn(input, "ims", NULL);
  qmi_message_wds_start_network_input_set_profile_index_3gpp(
      input, ctx->profile_id, NULL);
  qmi_message_wds_start_network_input_set_ip_family_preference(
      input, QMI_WDS_IP_FAMILY_IPV4, NULL);
  qmi_message_wds_start_network_input_set_enable_autoconnect(input, TRUE, NULL);
  g_debug("Asynchronously starting network...");
  qmi_client_wds_start_network(ctx->client, input, 180, ctx->cancellable,
                               (GAsyncReadyCallback)start_network_ready, NULL);
  if (input)
    qmi_message_wds_start_network_input_unref(input);

  ctx->connection_readiness_step++;
}

/*
 * Modify the first profile, which seems to be empty all the time
 * Maybe a better approach would be to find the first empty slot
 * first, then add or modify. But I'm not worrying about that
 * for now.
 */

static void modify_profile_ready(QmiClientWds *client, GAsyncResult *res) {
  QmiMessageWdsModifyProfileOutput *output;
  GError *error = NULL;

  output = qmi_client_wds_modify_profile_finish(client, res, &error);
  if (!output) {
    g_printerr("error: operation failed: %s\n", error->message);
    g_error_free(error);
    return;
  }

  if (!qmi_message_wds_modify_profile_output_get_result(output, &error)) {
    QmiWdsDsProfileError ds_profile_error;

    if (g_error_matches(error, QMI_PROTOCOL_ERROR,
                        QMI_PROTOCOL_ERROR_EXTENDED_INTERNAL) &&
        qmi_message_wds_modify_profile_output_get_extended_error_code(
            output, &ds_profile_error, NULL)) {
      g_printerr("error: couldn't modify profile: ds profile error: %s\n",
                 qmi_wds_ds_profile_error_get_string(ds_profile_error));
    } else {
      g_printerr("error: couldn't modify profile: %s\n", error->message);
    }
    g_error_free(error);
    qmi_message_wds_modify_profile_output_unref(output);
    return;
  }
  qmi_message_wds_modify_profile_output_unref(output);
  g_print("Profile successfully modified.\n");
}

void modify_profile_by_id(guint8 profile_id) {
  QmiMessageWdsModifyProfileInput *input = NULL;
  g_info("Modify profile ID %u\n", profile_id);
  input = qmi_message_wds_modify_profile_input_new();
  /* We're going to hardcode the fuck out of this for now */
  qmi_message_wds_modify_profile_input_set_profile_identifier(
      input, PROFILE_TYPE_3GPP, 1, NULL);

  //  qmi_message_wds_modify_profile_input_set_profile_type(input, 0, NULL);
  qmi_message_wds_modify_profile_input_set_pdp_context_number(input, profile_id,
                                                              NULL);
  qmi_message_wds_modify_profile_input_set_pdp_type(input, PDP_TYPE_IPV4,
                                                    NULL);
  qmi_message_wds_modify_profile_input_set_apn_type_mask(
      input, APN_TYPE_MASK_IMS, NULL);
  qmi_message_wds_modify_profile_input_set_profile_name(input, "ims", NULL);
  qmi_message_wds_modify_profile_input_set_apn_name(input, "ims", NULL);
  qmi_message_wds_modify_profile_input_set_authentication(input, 0, NULL);
  qmi_message_wds_modify_profile_input_set_username(input, "", NULL);
  qmi_message_wds_modify_profile_input_set_password(input, "", NULL);
  qmi_message_wds_modify_profile_input_set_roaming_disallowed_flag(input, 0,
                                                                   NULL);
  qmi_message_wds_modify_profile_input_set_apn_disabled_flag(input, 0, NULL);

  qmi_client_wds_modify_profile(ctx->client, input, 10, ctx->cancellable,
                                (GAsyncReadyCallback)modify_profile_ready,
                                NULL);
  qmi_message_wds_modify_profile_input_unref(input);
  ctx->connection_readiness_step = WDS_CONNECTION_STATE_PROFILE_READY;
  return;
}

static void add_new_profile_ready(QmiClientWds *client, GAsyncResult *res) {
  QmiMessageWdsCreateProfileOutput *output;
  GError *error = NULL;
  QmiWdsProfileType profile_type;

  output = qmi_client_wds_create_profile_finish(client, res, &error);
  if (!output) {
    g_printerr("error: operation failed: %s\n", error->message);
    g_error_free(error);
    return;
  }

  if (!qmi_message_wds_create_profile_output_get_result(output, &error)) {
    QmiWdsDsProfileError ds_profile_error;

    if (g_error_matches(error, QMI_PROTOCOL_ERROR,
                        QMI_PROTOCOL_ERROR_EXTENDED_INTERNAL) &&
        qmi_message_wds_create_profile_output_get_extended_error_code(
            output, &ds_profile_error, NULL)) {
      g_printerr("error: couldn't create profile: ds profile error: %s\n",
                 qmi_wds_ds_profile_error_get_string(ds_profile_error));
    } else {
      g_printerr("error: couldn't create profile: %s\n", error->message);
    }
    g_error_free(error);
    qmi_message_wds_create_profile_output_unref(output);
    return;
  }

  g_print("New profile created:\n");
  if (qmi_message_wds_create_profile_output_get_profile_identifier(
          output, &profile_type, &ctx->profile_id, NULL)) {
    g_print(" Profile type: '%s'\n",
            qmi_wds_profile_type_get_string(profile_type));
    g_print(" Profile index: '%d'\n", ctx->profile_id);
    ctx->connection_readiness_step = WDS_CONNECTION_STATE_PROFILE_READY;
  }
  qmi_message_wds_create_profile_output_unref(output);
}

void add_new_profile() {
  QmiMessageWdsCreateProfileInput *input = NULL;
  g_info("Adding new IMS profile\n");
  input = qmi_message_wds_create_profile_input_new();

  /* We're going to hardcode the fuck out of this for now */
  qmi_message_wds_create_profile_input_set_profile_type(
      input, QMI_WDS_PROFILE_TYPE_3GPP, NULL);
  qmi_message_wds_create_profile_input_set_pdp_context_number(input, 1, NULL);
  qmi_message_wds_create_profile_input_set_pdp_type(input, PDP_TYPE_IPV4,
                                                    NULL);
  qmi_message_wds_create_profile_input_set_apn_type_mask(
      input, APN_TYPE_MASK_IMS, NULL);
  qmi_message_wds_create_profile_input_set_profile_name(input, "ims", NULL);
  qmi_message_wds_create_profile_input_set_apn_name(input, "ims", NULL);
  qmi_message_wds_create_profile_input_set_authentication(input, 0, NULL);
  qmi_message_wds_create_profile_input_set_username(input, "", NULL);
  qmi_message_wds_create_profile_input_set_password(input, "", NULL);
  qmi_message_wds_create_profile_input_set_roaming_disallowed_flag(input, 0,
                                                                   NULL);
  qmi_message_wds_create_profile_input_set_apn_disabled_flag(input, 0, NULL);

  qmi_client_wds_create_profile(ctx->client, input, 10, ctx->cancellable,
                                (GAsyncReadyCallback)add_new_profile_ready,
                                NULL);
  qmi_message_wds_create_profile_input_unref(input);
  return;
}

void qmi_set_data_format() {
  g_printerr("*** %s: dummy\n", __func__);
  ctx->connection_readiness_step++;
}

static void wds_connection_setup_link_ready(QmiDevice *device,
                                            GAsyncResult *res) {
  g_print("[[[[[[[[[WDS ]]]]]]]]] %s\n", __func__);
    GError *error = NULL;
    g_printerr("[WDS] Setup Link Ready. No link name yet\n");
    ctx->link_name = qmi_device_add_link_with_flags_finish(
        device, res, &ctx->mux_id, &error);

    if (!ctx->link_name) {
      g_prefix_error(&error, "[WDS][FATAL] Failed to add the link name to the context\n");

    } else {
      g_print("[WDS] Link Ready: name: %s, mux id: %u\n", ctx->link_name, ctx->mux_id);
      ctx->connection_readiness_step++;
    }

}

void wds_connection_setup_link() {
  /* For some reason we get a MUX ID bigger than the link name all the time */
  QmiDeviceAddLinkFlags flags = (QMI_DEVICE_ADD_LINK_FLAGS_INGRESS_MAP_CKSUMV4 |
                                 QMI_DEVICE_ADD_LINK_FLAGS_EGRESS_MAP_CKSUMV4);
  if (!ctx->setup_link_done) {
    g_print("[WDS] Set link with flags!\n");
    qmi_device_add_link_with_flags(
        ctx->device, QMI_DEVICE_MUX_ID_AUTOMATIC, "rmnet_ipa0", "qmapmux0.",
        flags, NULL, (GAsyncReadyCallback)wds_connection_setup_link_ready,
        NULL);
    ctx->setup_link_done = 1;
  } else {
    g_print("[WDS] Ignoring Spurious call\n");
  }
}

static void bind_mux_data_port_ready(QmiClientWds *client, GAsyncResult *res,
                                     GTask *task) {
  GError *error = NULL;
  g_autoptr(QmiMessageWdsBindMuxDataPortOutput) output = NULL;
  g_print("*** %s\n", __func__);
  output = qmi_client_wds_bind_mux_data_port_finish(client, res, &error);
  if (!output ||
      !qmi_message_wds_bind_mux_data_port_output_get_result(output, &error)) {
    g_print("Couldn't bind mux data port: %s", error->message);
    return;
  }

  /* Keep on */
  ctx->connection_readiness_step++;
}

void bind_data_port() {
  g_autoptr(QmiMessageWdsBindMuxDataPortInput) input = NULL;
  ctx->endpoint_ifnum = 1;
  ctx->endpoint_type = QMI_DATA_ENDPOINT_TYPE_EMBEDDED;
  g_print("binding to mux id %d", ctx->mux_id);

  input = qmi_message_wds_bind_mux_data_port_input_new();

  qmi_message_wds_bind_mux_data_port_input_set_endpoint_info(
      input, ctx->endpoint_type, ctx->endpoint_ifnum, NULL);
  qmi_message_wds_bind_mux_data_port_input_set_mux_id(input, ctx->mux_id, NULL);

  qmi_client_wds_bind_mux_data_port(
      ctx->client, input, 10, ctx->cancellable,
      (GAsyncReadyCallback)bind_mux_data_port_ready, NULL);
}

/** GET SETTINGS ***/
static void qmi_inet4_ntop(guint32 address, char *buf, const gsize buflen) {
  struct in_addr a = {.s_addr = GUINT32_TO_BE(address)};

  g_assert(buflen >= INET_ADDRSTRLEN);

  /* We can ignore inet_ntop() return value if 'buf' is
   * at least INET_ADDRSTRLEN in size. */
  memset(buf, 0, buflen);
  g_assert(inet_ntop(AF_INET, &a, buf, buflen));
}

guint mm_count_bits_set(gulong number) {
  guint c;

  for (c = 0; number; c++)
    number &= number - 1;
  return c;
}

guint mm_find_bit_set(gulong number) {
  guint c = 0;

  for (c = 0; !(number & 0x1); c++)
    number >>= 1;
  return c;
}

guint mm_netmask_to_cidr(const gchar *netmask) {
  guint32 num = 0;

  inet_pton(AF_INET, netmask, &num);
  return mm_count_bits_set(num);
}

void calculate_subnet(char *ip_address, char *netmask, char *subnet) {
  struct in_addr ip, mask, subnet_addr;

  // Convert IP and netmask strings to binary form
  if (inet_pton(AF_INET, ip_address, &ip) != 1) {
    fprintf(stderr, "Invalid IP address: %s\n", ip_address);
    exit(EXIT_FAILURE);
  }

  if (inet_pton(AF_INET, netmask, &mask) != 1) {
    fprintf(stderr, "Invalid netmask: %s\n", netmask);
    exit(EXIT_FAILURE);
  }

  // Calculate subnet address by bitwise AND
  subnet_addr.s_addr = ip.s_addr & mask.s_addr;

  // Convert subnet address to dot-decimal notation
  if (inet_ntop(AF_INET, &subnet_addr, subnet, INET_ADDRSTRLEN) == NULL) {
    perror("inet_ntop");
    exit(EXIT_FAILURE);
  }
  g_print(" - Subnet: %s\n", subnet);
}

static void get_current_settings_ready(QmiClientWds *client,
                                       GAsyncResult *res) {
  GError *error = NULL;
  QmiMessageWdsGetCurrentSettingsOutput *output;
  char buf[16];
  char address[32];
  char network[32];
  char netmask[32];
  guint32 addr = 0;
  guint32 prefix = 0;
  QmiWdsIpFamily ip_family = QMI_WDS_IP_FAMILY_UNSPECIFIED;
  guint32 mtu = 0;
  char temp[128] = {0};

  output = qmi_client_wds_get_current_settings_finish(client, res, &error);
  if (!output ||
      !qmi_message_wds_get_current_settings_output_get_result(output, &error)) {

    /* Otherwise, just go on as we're asking for DHCP */
    g_print("couldn't get current settings: %s", error->message);
    g_error_free(error);
  }

  if (!qmi_message_wds_get_current_settings_output_get_ip_family(
          output, &ip_family, &error)) {
    g_print(" IP Family: failed (%s); assuming IPv4", error->message);
    g_clear_error(&error);
    ip_family = QMI_WDS_IP_FAMILY_IPV4;
  }
  g_print(" - IP Family: %s \n", (ip_family == QMI_WDS_IP_FAMILY_IPV4) ? "IPv4"
                                 : (ip_family == QMI_WDS_IP_FAMILY_IPV6)
                                     ? "IPv6"
                                     : "unknown");

  if (!qmi_message_wds_get_current_settings_output_get_mtu(output, &mtu,
                                                           &error)) {
    g_print("MTU: failed (%s)", error->message);
    g_clear_error(&error);
  } else {
    g_print(" - MTU: %d\n", mtu);
  }
  /*
          if (ip_family == QMI_WDS_IP_FAMILY_IPV4)
              ctx->ipv4_config = get_ipv4_config (ctx->self, ctx->ip_method,
     output, mtu); else if (ip_family == QMI_WDS_IP_FAMILY_IPV6)
              ctx->ipv6_config = get_ipv6_config (ctx->self, ctx->ip_method,
     output, mtu);*/

  //    process_operator_reserved_pco (self, output);

  /* IPv4 subnet mask */
  if (!qmi_message_wds_get_current_settings_output_get_ipv4_gateway_subnet_mask(
          output, &addr, &error)) {
    g_print("failed to read IPv4 netmask: %s\n", error->message);
    g_clear_error(&error);
    return;
  }

  qmi_inet4_ntop(addr, buf, sizeof(buf));
  prefix = mm_netmask_to_cidr(buf);
  strncpy(netmask, buf, sizeof(netmask));
  /* IPv4 address */
  if (!qmi_message_wds_get_current_settings_output_get_ipv4_address(
          output, &addr, &error)) {
    g_print("IPv4 family but no IPv4 address: %s\n", error->message);
    g_clear_error(&error);
    return;
  }
  /* IPv4 address */

  qmi_inet4_ntop(addr, buf, sizeof(buf));
  g_print("IPv4 Address: %s/%d\n", buf, prefix);
  strncpy(address, buf, sizeof(address));
  strncpy((char*)ctx->ip_address, buf, sizeof(address));
  sprintf(temp, "ip addr add %s/%d dev %s ", buf, prefix, ctx->link_name);
  g_print("Executing system command %s \n", temp);
  //system(temp); // LIKE ANIMALS
  calculate_subnet(address, netmask, network);

  /* IPv4 gateway address */
  if (qmi_message_wds_get_current_settings_output_get_ipv4_gateway_address(
          output, &addr, &error)) {
    qmi_inet4_ntop(addr, buf, sizeof(buf));
    g_print(" - IPv4 Gateway: %s\n", buf);
  } else {
    g_print("- IPv4 Gateway: failed (%s)\n", error->message);
    g_clear_error(&error);
  }

  temp[0] = 0x00;
  sprintf(temp, "ip route change %s/%d via %s dev %s ", network, prefix, buf,
          ctx->link_name);
  g_print("Executing system command %s \n", temp);
 // system(temp); // LIKE ANIMALS

  if (output)
    qmi_message_wds_get_current_settings_output_unref(output);

  /* Keep on */
  ctx->connection_readiness_step++;
}

static void get_current_settings() {
  QmiMessageWdsGetCurrentSettingsInput *input;
  QmiWdsRequestedSettings requested;

  requested = QMI_WDS_REQUESTED_SETTINGS_DNS_ADDRESS |
              QMI_WDS_REQUESTED_SETTINGS_GRANTED_QOS |
              QMI_WDS_REQUESTED_SETTINGS_IP_ADDRESS |
              QMI_WDS_REQUESTED_SETTINGS_GATEWAY_INFO |
              QMI_WDS_REQUESTED_SETTINGS_MTU |
              QMI_WDS_REQUESTED_SETTINGS_DOMAIN_NAME_LIST |
              QMI_WDS_REQUESTED_SETTINGS_IP_FAMILY |
              QMI_WDS_REQUESTED_SETTINGS_OPERATOR_RESERVED_PCO;

  input = qmi_message_wds_get_current_settings_input_new();
  qmi_message_wds_get_current_settings_input_set_requested_settings(
      input, requested, NULL);
  qmi_client_wds_get_current_settings(
      ctx->client, input, 10, NULL,
      (GAsyncReadyCallback)get_current_settings_ready, NULL);
  qmi_message_wds_get_current_settings_input_unref(input);
}

/*** END OF GET SETTINGS *****/
gboolean get_wds_ready_to_connect() {
  g_printerr("[WDS] Readiness step: %u\n", ctx->connection_readiness_step);
  switch (ctx->connection_readiness_step) {
  case WDS_CONNECTION_GET_PROFILES:
    g_printerr("[WDS] WDS_CONNECTION_GET_PROFILES\n");
    get_profile_list("3gpp");
    break;
  case WDS_CONNECTION_FIND_PROFILE_ADD_MODIFY:
    g_printerr("[WDS] WDS_CONNECTION_FIND_PROFILE_ADD_MODIFY\n");
    if (ctx->profile_id == 0) {
      add_new_profile();
    } else {
      modify_profile_by_id(ctx->profile_id);
    }
    break;
  case WDS_CONNECTION_STATE_PROFILE_READY:
    g_printerr("[WDS] WDS_CONNECTION_STATE_PROFILE_READY: Profile ID is %u\n",
               ctx->profile_id);
    ctx->connection_readiness_step = WDS_CONNECTION_STATE_SETUP_DATA_FORMAT;
    break;
  case WDS_CONNECTION_STATE_SETUP_DATA_FORMAT:
    g_printerr("[WDS] WDS_CONNECTION_STATE_SETUP_DATA_FORMAT\n");
    qmi_set_data_format();
    break;
  case WDS_CONNECTION_STATE_SETUP_LINK:
    g_printerr("[WDS] WDS_CONNECTION_STATE_SETUP_LINK\n");
    wds_connection_setup_link();
    break;
  case WDS_CONNECTION_STATE_LINK_BRINGUP:
    g_printerr("[WDS] WDS_CONNECTION_STATE_LINK_BRINGUP\n");
    char temp[32] = {0};
    sprintf(temp, "ip link set %s up", ctx->link_name);
    g_print("Executing system command %s\n", temp);
    // system(temp); // LIKE ANIMALS
    ctx->connection_readiness_step++;
    break;
  case WDS_CONNECTION_STATE_SET_IP_BEARER_METHOD:
    g_printerr("[WDS] WDS_CONNECTION_STATE_SET_IP_BEARER_METHOD [dummy]\n");
    ctx->connection_readiness_step++;
    break;
  case WDS_CONNECTION_STATE_BIND_DATA_PORT_IPV4:
    g_printerr("[WDS] WDS_CONNECTION_STATE_BIND_DATA_PORT_IPV4\n");
    bind_data_port();
    break;
  case WDS_CONNECTION_STATE_SELECT_IP_FAMILY_IPV4:
    g_printerr("[WDS] WDS_CONNECTION_STATE_SELECT_IP_FAMILY_IPV4 [dummy]\n");
    ctx->connection_readiness_step++;
    break;
  case WDS_CONNECTION_STATE_DO_START_NETWORK_IPV4:
    g_printerr("[WDS] WDS_CONNECTION_STATE_DO_START_NETWORK_IPV4\n");
    wds_start_network();
    break;
  case WDS_CONNECTION_STATE_WAIT_FOR_COMPLETION_NET_START_IPV4:
    g_printerr("Waiting for response...");
    sleep(1);
    break;
  case WDS_CONNECTION_STATE_REGISTER_WDS_INDICATIONS_IPV4:
    g_printerr("[WDS] WDS_CONNECTION_STATE_REGISTER_WDS_INDICATIONS_IPV4\n");
    ctx->connection_readiness_step++;
    break;
  case WDS_CONNECTION_STATE_GET_SETTINGS_IPV4:
    g_printerr("[WDS] WDS_CONNECTION_STATE_GET_SETTINGS_IPV4\n");
    get_current_settings();
    break;
  case WDS_CONNECTION_STATE_BIND_DATA_PORT_IPV6:
    g_printerr("[WDS] WDS_CONNECTION_STATE_BIND_DATA_PORT_IPV6 [stop here]\n");
    ctx->connection_readiness_step = 99;
    break;
  case WDS_CONNECTION_STATE_SELECT_IP_FAMILY_IPV6:
    g_printerr("[WDS] WDS_CONNECTION_STATE_SELECT_IP_FAMILY_IPV6\n");
    break;
  case WDS_CONNECTION_STATE_DO_START_NETWORK_IPV6:
    g_printerr("[WDS] WDS_CONNECTION_STATE_DO_START_NETWORK_IPV6\n");
    break;
  case WDS_CONNECTION_STATE_ENABLE_INDICATIONS_IPV6:
    g_printerr("[WDS] WDS_CONNECTION_STATE_ENABLE_INDICATIONS_IPV6\n");
    break;
  case WDS_CONNECTION_STATE_GET_SETTINGS_IPV6:
    g_printerr("[WDS] WDS_CONNECTION_STATE_GET_SETTINGS_IPV6\n");
    break;
  case WDS_CONNECTION_STATE_FINISHED:
    g_print("[WDS] Bringup finished\n");
    return FALSE;
  default:
    g_info("We hit the default case, giving up\n");
    return FALSE;
    break;
  }

  return TRUE;
}
/*
 * Hooks to the qmi client
 *
 *
 */
void wds_start(QmiDevice *device, QmiClientWds *client,
               GCancellable *cancellable) {

  /* Initialize context */
  // GTask *task;
  ctx = g_slice_new(Context);
  ctx->device = g_object_ref(device);
  ctx->client = g_object_ref(client);
  ctx->cancellable = g_object_ref(cancellable);
  ctx->packet_data_handle = 0xFFFFFFFF;
  //  get_autoconnect_settings();
  ctx->connection_readiness_step = 0;
  ctx->profile_id = 0;
  ctx->setup_link_done = 0;
  ctx->mux_id = 0;
  g_timeout_add(5, get_wds_ready_to_connect, NULL);
  g_timeout_add(10, get_pkt_svc_status, NULL);
}