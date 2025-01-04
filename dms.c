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
  QmiClientDms *client;
  GCancellable *cancellable;
  gboolean enable;
} Context;

static Context *ctx;

/* We aren't doing anything with this yet */
void dms_get_capabilities_finish(QmiClientDms *client, GAsyncResult *res) {
  GError *error = NULL;
  g_print("[DMS] Get capabilities callback\n");
  QmiMessageDmsGetCapabilitiesOutput *output;
  output = qmi_client_dms_get_capabilities_finish(client, res, &error);
  if (!output) {
    g_print("FATAL: Output seems empty!\n");
  }

  if (!qmi_message_dms_get_capabilities_output_get_result(output, &error)) {
    g_print("FATAL: Error getting capabilities!\n");
  }

  if (output)
    qmi_message_dms_get_capabilities_output_unref(output);
}

void dms_indication_register_finish(QmiClientDms *client, GAsyncResult *res) {
  GError *error = NULL;
  g_print("[DMS] Indication Register finish\n");
  QmiMessageDmsIndicationRegisterOutput *output;
  output = qmi_client_dms_indication_register_finish(client, res, &error);
  if (!output) {
    g_print("FATAL: Output seems empty!\n");
  }

  if (!qmi_message_dms_indication_register_output_get_result(output, &error)) {
    g_print("FATAL: Error getting capabilities!\n");
  }

  if (output)
    qmi_message_dms_indication_register_output_unref(output);
}
void dms_get_capabilities() {
  g_print("Read DMS capabilities.\n");
  qmi_client_dms_get_capabilities(
      ctx->client, NULL, 5, NULL,
      (GAsyncReadyCallback)dms_get_capabilities_finish, NULL);
}

void dms_indication_register() {
  g_print("[DMS] Request Indication Register.\n");
  QmiMessageDmsIndicationRegisterInput *input;
  guint8 report_psm_status = 0x01;
  guint8 report_psm_config_change = 0x01;
  guint8 report_ims_capability = 0x01;
  input = qmi_message_dms_indication_register_input_new();
  qmi_message_dms_indication_register_input_set_psm_status(
      input, report_psm_status, NULL);
  qmi_message_dms_indication_register_input_set_psm_config_change(
      input, report_psm_config_change, NULL);
  qmi_message_dms_indication_register_input_set_ims_capability(
      input, report_ims_capability, NULL);
  qmi_client_dms_indication_register(
      ctx->client, input, 10, NULL,
      (GAsyncReadyCallback)dms_indication_register_finish, NULL);
}

void dms_start(QmiDevice *device, QmiClientDms *client,
               GCancellable *cancellable) {

  /* Initialize context */
  ctx = g_slice_new(Context);
  ctx->device = g_object_ref(device);
  ctx->client = g_object_ref(client);
  ctx->cancellable = g_object_ref(cancellable);
  ctx->enable = TRUE;
  dms_get_capabilities();
  dms_indication_register();
}