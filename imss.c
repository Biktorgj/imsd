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

/* Context */
typedef struct {
  QmiDevice *device;
  QmiClientIms *client;
  GCancellable *cancellable;
} Context;

static Context *ctx;

static void attempt_start_imss_services_ready(QmiClientIms *client,
                                             GAsyncResult *res) {
  QmiMessageImsSetImsServiceEnableConfigOutput *output;
  GError *error = NULL;
  g_info("********* IMS SERVICES READY RESPONSE\n");
  output =
      qmi_client_ims_set_ims_service_enable_config_finish(client, res, &error);
  if (!output) {
    g_printerr("error: operation failed: %s\n", error->message);
    g_error_free(error);
    return;
  }

  if (!qmi_message_ims_set_ims_service_enable_config_output_get_result(
          output, &error)) {
    g_printerr("It told me to fuck off %s\n", error->message);
    g_error_free(error);
    qmi_message_ims_set_ims_service_enable_config_output_unref(output);
    return;
  }
}

void attempt_start_imss_services() {
  QmiMessageImsSetImsServiceEnableConfigInput *input;
  GError *error = NULL;

  g_info("Trying to start IMS service with our own stuff!\n");
  input = qmi_message_ims_set_ims_service_enable_config_input_new();
  qmi_message_ims_set_ims_service_enable_config_input_set_volte_status(
      input, 1, &error);
  qmi_message_ims_set_ims_service_enable_config_input_set__rtt_service_status_(
      input, 1, &error);
  qmi_message_ims_set_ims_service_enable_config_input_set_call_mode_preference_roaming_service_status(
      input, 1, &error);
  qmi_message_ims_set_ims_service_enable_config_input_set_rcs_messaging_service_status(
      input, 1, &error);
  qmi_message_ims_set_ims_service_enable_config_input_set_xdm_client_status(
      input, 1, &error);
  qmi_message_ims_set_ims_service_enable_config_input_set_autoconfig_service_status(
      input, 1, &error);
  qmi_message_ims_set_ims_service_enable_config_input_set_presence_service_status(
      input, 1, &error);
  qmi_message_ims_set_ims_service_enable_config_input_set_ims_service_status(
      input, 1, &error);
  qmi_message_ims_set_ims_service_enable_config_input_set_enable_wifi_calling_support_in_roaming_through_client_provisioning(
      input, 1, &error);
  qmi_client_ims_set_ims_service_enable_config(
      ctx->client, input, 10, ctx->cancellable,
      (GAsyncReadyCallback)attempt_start_imss_services_ready, NULL);
}

void imss_start(QmiDevice *device, QmiClientIms *client,
                GCancellable *cancellable) {

  /* Initialize context */
  ctx = g_slice_new(Context);
  ctx->device = g_object_ref(device);
  ctx->client = g_object_ref(client);
  ctx->cancellable = g_object_ref(cancellable);
  attempt_start_imss_services();
}