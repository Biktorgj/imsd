/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Copyright (c) 2024, Biktorgj
 */
#include "conn-manager.h"
#include <gio/gio.h>
#include <glib-unix.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <libqmi-glib.h>
#include <libqrtr-glib.h>

/* Context */
typedef struct {
  QmiDevice *device;
  QmiClientWds *client;
  GCancellable *cancellable;
} Context;
static Context *ctx;

/**** EXAMPLES ************/

static void get_packet_service_status_ready(QmiClientWds *client, GAsyncResult *res) {
  GError *error = NULL;
  QmiMessageWdsGetPacketServiceStatusOutput *output;
  QmiWdsConnectionStatus status;

  output = qmi_client_wds_get_packet_service_status_finish(client, res, &error);
  if (!output) {
    g_printerr("error: operation failed: %s\n", error->message);
    g_error_free(error);
    return;
  }

  if (!qmi_message_wds_get_packet_service_status_output_get_result(output, &error)) {
    g_printerr("error: couldn't get packet service status: %s\n", error->message);
    g_error_free(error);
    qmi_message_wds_get_packet_service_status_output_unref(output);
    return;
  }

  qmi_message_wds_get_packet_service_status_output_get_connection_status(output, &status, NULL);

  g_print("[%s] Connection status: '%s'\n", qmi_device_get_path_display(ctx->device), qmi_wds_connection_status_get_string(status));

  qmi_message_wds_get_packet_service_status_output_unref(output);
}

void get_pkt_svc_status() {
  g_printerr("Asynchronously getting packet service status...");
  qmi_client_wds_get_packet_service_status(
      ctx->client, NULL, 10, ctx->cancellable,
      (GAsyncReadyCallback)get_packet_service_status_ready, NULL);
}
static void get_autoconnect_settings_ready(QmiClientWds *client, GAsyncResult *res) {
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
    g_printerr("error: couldn't get autoconnect settings: %s\n", error->message);
    g_error_free(error);
    qmi_message_wds_get_autoconnect_settings_output_unref(output);
    return;
  }

  g_print("Autoconnect settings retrieved:\n");
  qmi_message_wds_get_autoconnect_settings_output_get_status(output, &status, NULL);
  g_print("\tStatus: '%s'\n", qmi_wds_autoconnect_setting_get_string(status));

  if (qmi_message_wds_get_autoconnect_settings_output_get_roaming(output, &roaming, NULL))
    g_print("\tRoaming: '%s'\n", qmi_wds_autoconnect_setting_roaming_get_string(roaming));

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
}