/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Copyright (c) 2024, Biktorgj
 */
#include "qmi-ims-client.h"
#include <gio/gio.h>
#include <glib-unix.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <libqmi-glib.h>
#include <libqrtr-glib.h>

#define VALIDATE_MASK_NONE(str) (str ? str : "none")

/* Context */
typedef struct {
  QmiDevice *device;
  QmiClientWds *client;
  GCancellable *cancellable;
} Context;
static Context *ctx;

/********** PROFILES ************/

typedef struct {
  guint i;
  GArray *profile_list;
} GetProfileListContext;

static void get_next_profile_settings(GetProfileListContext *inner_ctx);

static void get_profile_settings_ready(QmiClientWds *client, GAsyncResult *res,
                                       GetProfileListContext *inner_ctx) {
  QmiMessageWdsGetProfileSettingsOutput *output;
  GError *error = NULL;

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
      g_print("\t\tAPN: '%s'\n", str);
    if (qmi_message_wds_get_profile_settings_output_get_apn_type_mask(
            output, &apn_type, NULL)) {
      g_autofree gchar *aux = NULL;

      aux = qmi_wds_apn_type_mask_build_string_from_mask(apn_type);
      g_print("\t\tAPN type: '%s'\n", VALIDATE_MASK_NONE(aux));
    }
    if (qmi_message_wds_get_profile_settings_output_get_pdp_type(
            output, &pdp_type, NULL))
      g_print("\t\tPDP type: '%s'\n", qmi_wds_pdp_type_get_string(pdp_type));
    if (qmi_message_wds_get_profile_settings_output_get_pdp_context_number(
            output, &context_number, NULL))
      g_print("\t\tPDP context number: '%d'\n", context_number);
    if (qmi_message_wds_get_profile_settings_output_get_username(output, &str,
                                                                 NULL))
      g_print("\t\tUsername: '%s'\n", str);
    if (qmi_message_wds_get_profile_settings_output_get_password(output, &str,
                                                                 NULL))
      g_print("\t\tPassword: '%s'\n", str);
    if (qmi_message_wds_get_profile_settings_output_get_authentication(
            output, &auth, NULL)) {
      g_autofree gchar *aux = NULL;

      aux = qmi_wds_authentication_build_string_from_mask(auth);
      g_print("\t\tAuth: '%s'\n", VALIDATE_MASK_NONE(aux));
    }
    if (qmi_message_wds_get_profile_settings_output_get_roaming_disallowed_flag(
            output, &flag, NULL))
      g_print("\t\tNo roaming: '%s'\n", flag ? "yes" : "no");
    if (qmi_message_wds_get_profile_settings_output_get_apn_disabled_flag(
            output, &flag, NULL))
      g_print("\t\tAPN disabled: '%s'\n", flag ? "yes" : "no");
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
    g_array_unref(inner_ctx->profile_list);
    g_slice_free(GetProfileListContext, inner_ctx);
    return;
  }

  profile = &g_array_index(inner_ctx->profile_list,
                           QmiMessageWdsGetProfileListOutputProfileListProfile,
                           inner_ctx->i);
  g_print("\t[%u] %s - %s\n", profile->profile_index,
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
    g_print("Profile list empty\n");
    qmi_message_wds_get_profile_list_output_unref(output);
    return;
  }

  g_print("Profile list retrieved:\n");

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

  g_debug("Asynchronously get profile list...\n");
  qmi_client_wds_get_profile_list(ctx->client, input, 10, ctx->cancellable,
                                  (GAsyncReadyCallback)get_profile_list_ready,
                                  NULL);
  qmi_message_wds_get_profile_list_input_unref(input);
  return;
}
/************ PROFILES *************/

/**** EXAMPLES ************/

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

  g_print("[%s] Connection status: '%s'\n",
          qmi_device_get_path_display(ctx->device),
          qmi_wds_connection_status_get_string(status));

  qmi_message_wds_get_packet_service_status_output_unref(output);
}

void get_pkt_svc_status() {
  g_printerr("Asynchronously getting packet service status...\n");
  qmi_client_wds_get_packet_service_status(
      ctx->client, NULL, 10, ctx->cancellable,
      (GAsyncReadyCallback)get_packet_service_status_ready, NULL);
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
  g_print("\tStatus: '%s'\n", qmi_wds_autoconnect_setting_get_string(status));

  if (qmi_message_wds_get_autoconnect_settings_output_get_roaming(
          output, &roaming, NULL))
    g_print("\tRoaming: '%s'\n",
            qmi_wds_autoconnect_setting_roaming_get_string(roaming));

  qmi_message_wds_get_autoconnect_settings_output_unref(output);
}

void get_autoconnect_settings() {
  qmi_client_wds_get_autoconnect_settings(
      ctx->client, NULL, 10, ctx->cancellable,
      (GAsyncReadyCallback)get_autoconnect_settings_ready, NULL);
}

/**** EXAMPLES ************/

void wds_start(QmiDevice *device, QmiClientWds *client,
               GCancellable *cancellable) {

  /* Initialize context */
  ctx = g_slice_new(Context);
  ctx->device = g_object_ref(device);
  ctx->client = g_object_ref(client);
  ctx->cancellable = g_object_ref(cancellable);
  get_autoconnect_settings();
  get_pkt_svc_status();
  get_profile_list("3gpp");
}