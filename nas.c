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
  QmiClientNas *client;
  GCancellable *cancellable;
  gboolean enable;
} Context;

static Context *ctx;

static Carrier current_carrier;

Carrier get_carrier_data() {
  return current_carrier;
}

static void get_home_network_ready(QmiClientNas *client, GAsyncResult *res) {
  QmiMessageNasGetHomeNetworkOutput *output;
  GError *error = NULL;

  output = qmi_client_nas_get_home_network_finish(client, res, &error);
  if (!output) {
    g_printerr("error: operation failed: %s\n", error->message);
    g_error_free(error);
    return;
  }

  if (!qmi_message_nas_get_home_network_output_get_result(output, &error)) {
    g_printerr("error: couldn't get home network: %s\n", error->message);
    g_error_free(error);
    qmi_message_nas_get_home_network_output_unref(output);
    return;
  }
  guint16 mcc;
  guint16 mnc;
  const gchar *description;

  qmi_message_nas_get_home_network_output_get_home_network(output, &mcc, &mnc,
                                                           &description, NULL);

  g_print("Home network: '%" G_GUINT16_FORMAT "'-'%" G_GUINT16_FORMAT "'\n",
          mcc, mnc);
  current_carrier.mcc = mcc;
  current_carrier.mnc = mnc;

  qmi_message_nas_get_home_network_output_unref(output);
}

void get_home_network() {
  g_debug("Asynchronously getting home network...");
  qmi_client_nas_get_home_network(ctx->client, NULL, 10, ctx->cancellable,
                                  (GAsyncReadyCallback)get_home_network_ready,
                                  NULL);
  return;
}
/* 
  EVENT REPORT TESTING: First attempt
  So, we need to know if signal gets lost, recovered, or somethng fails
  in between. Then we need to get event reports from the NAS service.
  This is the first test to see if we can tell the baseband to inform us
  about those things. From there we can go on with the rest.
*/

static void get_event_report_ready(QmiClientNas *client, GAsyncResult *res) {
  QmiMessageNasSetEventReportOutput *output;
  GError *error = NULL;
  g_debug("%s: We have been called!\n", __func__);
  output = qmi_client_nas_set_event_report_finish(client, res, &error);
  if (!output) {
    g_printerr("error: operation failed: %s\n", error->message);
    g_error_free(error);
    return;
  } else {
    g_debug("%s The command went through!\n", __func__);
  }
}


void set_event_report() {
  g_debug("Enabling event reporting for NAS: Test 1\n");
  qmi_client_nas_set_event_report(ctx->client, NULL, 10, ctx->cancellable,
                                  (GAsyncReadyCallback)get_event_report_ready,
                                  NULL);
}

/* Indications */

static void ri_serving_system_or_system_info_ready(QmiClientNas *client,
                                                   GAsyncResult *res) {
  QmiMessageNasRegisterIndicationsOutput *output;
  GError *error = NULL;
  g_debug("[%s] We have been called!\n", __func__);
  output = qmi_client_nas_register_indications_finish(client, res, &error);
  if (!output) {
    g_printerr("error: operation failed: %s\n", error->message);
    g_error_free(error);
    return;
  } else {
    g_debug("%s The command went through!\n", __func__);
  }
}

void enable_nas_indications() {
  g_debug("* Enabling NAS indications: Test 2\n");
  g_autoptr(QmiMessageNasRegisterIndicationsInput) input = NULL;

  input = qmi_message_nas_register_indications_input_new();

  qmi_message_nas_register_indications_input_set_serving_system_events(
      input, FALSE, NULL);

  qmi_message_nas_register_indications_input_set_network_reject_information(
      input, ctx->enable, FALSE, NULL);
  qmi_client_nas_register_indications(
      ctx->client, input, 5, NULL,
      (GAsyncReadyCallback)ri_serving_system_or_system_info_ready, NULL);
}

void nas_start(QmiDevice *device, QmiClientNas *client,
               GCancellable *cancellable) {

  /* Initialize context */
  ctx = g_slice_new(Context);
  ctx->device = g_object_ref(device);
  ctx->client = g_object_ref(client);
  ctx->cancellable = g_object_ref(cancellable);
  ctx->enable = TRUE;
  get_home_network();
  set_event_report();
  enable_nas_indications();
}