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
  QmiClientImsa *client;
  GCancellable *cancellable;
  guint8 sub_requested;
} Context;

static Context *ctx;

guint8 is_sub_requested() {
  if (ctx->sub_requested)
    return ctx->sub_requested;
  else {
    ctx->sub_requested = 1;
    return 0;
  }
}

static void get_ims_registration_status_ready(QmiClientImsa *client,
                                              GAsyncResult *res) {
  QmiMessageImsaGetImsRegistrationStatusOutput *output;
  QmiImsaImsRegistrationStatus registration_status;
  GError *error = NULL;

  output =
      qmi_client_imsa_get_ims_registration_status_finish(client, res, &error);
  if (!output) {
    g_printerr("%s: error: operation failed: %s\n", __func__, error->message);
    g_error_free(error);
    return;
  }

  if (!qmi_message_imsa_get_ims_registration_status_output_get_result(output,
                                                                      &error)) {
    g_printerr("%s: error: couldn't get IMS registration status: %s\n",
               __func__, error->message);
    g_error_free(error);
    qmi_message_imsa_get_ims_registration_status_output_unref(output);
    return;
  }

  /* Should add here multiple subscriptions*/
  g_print("   - IMS Status: ");

  if (qmi_message_imsa_get_ims_registration_status_output_get_ims_registration_status(
          output, &registration_status, NULL))  {
    switch (registration_status) {
      case 0:
      g_print("Not registered\n");
      break;
      case 1:
      g_print("Connection in progress\n");
      break;
      case 2:
      g_print("Registered\n");
      break;
      default:
      g_print("Limited service\n");
      break;
    }
  } else {
      g_print("Unavailable\n");
  }

  qmi_message_imsa_get_ims_registration_status_output_unref(output);
}

static void get_ims_services_status_ready(QmiClientImsa *client,
                                          GAsyncResult *res) {
  QmiMessageImsaGetImsServicesStatusOutput *output;
  QmiImsaServiceStatus service_sms_status;
  QmiImsaServiceStatus service_voice_status;
  QmiImsaServiceStatus service_vt_status;
  QmiImsaServiceStatus service_ut_status;
  QmiImsaServiceStatus service_vs_status;
  GError *error = NULL;

  output = qmi_client_imsa_get_ims_services_status_finish(client, res, &error);
  if (!output) {
    g_printerr("error: operation failed: %s\n", error->message);
    g_error_free(error);
    return;
  }

  if (!qmi_message_imsa_get_ims_services_status_output_get_result(output,
                                                                  &error)) {
    g_printerr("error: couldn't get IMS services status: %s\n", error->message);
    g_error_free(error);
    qmi_message_imsa_get_ims_services_status_output_unref(output);
    return;
  }

  g_print(" - IMS services:\n");

  g_print("   - SMS over IMS: ");

  if (qmi_message_imsa_get_ims_services_status_output_get_ims_sms_service_status(
          output, &service_sms_status, NULL)) {
    switch (service_sms_status) {
      case 0:
      g_print("Unavailable\n");
      break;
      case 1:
      g_print("Limited service\n");
      break;
      case 2:
      g_print("Ready\n");
      break;
      default:
      g_print("Unknown state\n");
      break;
    }
  } else {
      g_print("Legacy GSM mode\n");
  }

  g_print("   - VoIP service: ");

  if (qmi_message_imsa_get_ims_services_status_output_get_ims_voice_service_status(
          output, &service_voice_status, NULL)) {
    switch (service_voice_status) {
      case 0:
      g_print("Unavailable\n");
      break;
      case 1:
      g_print("Limited service\n");
      break;
      case 2:
      g_print("Ready\n");
      break;
      default:
      g_print("Unknown state\n");
      break;
    }
  } else {
      g_print("Legacy GSM mode\n");
  }
  g_print("   - Video Telephony service: ");

  if (qmi_message_imsa_get_ims_services_status_output_get_ims_video_telephony_service_status(
          output, &service_vt_status, NULL)) {    
      switch (service_vt_status) {
      case 0:
      g_print("Unavailable\n");
      break;
      case 1:
      g_print("Limited service\n");
      break;
      case 2:
      g_print("Ready\n");
      break;
      default:
      g_print("Unknown state\n");
      break;
    }
  } else {
      g_print("Legacy GSM mode\n");
  }
  g_print("   - UE to TAS service: ");

  if (qmi_message_imsa_get_ims_services_status_output_get_ims_ue_to_tas_service_status(
          output, &service_ut_status, NULL)){    
      switch (service_ut_status) {
      case 0:
      g_print("Unavailable\n");
      break;
      case 2:
      g_print("Ready\n");
      break;
      default:
      g_print("Unknown state (%u)\n", service_ut_status);
      break;
    }
  } else {
      g_print("Unavailable [hint: invalid PDC or incompatible RAT] \n");
  }
  g_print("   - Video Share service: ");
  if (qmi_message_imsa_get_ims_services_status_output_get_ims_video_share_service_status(
          output, &service_vs_status, NULL)) {
      switch (service_vs_status) {
      case 0:
      g_print("Running via WLAN\n");
      break;
      case 1:
      g_print("Running via WWAN\n");
      break;
      default:
      g_print("Unknown state (%u)\n", service_ut_status);
      break;
    }
  } else {
      g_print("Unavailable\n");
  }
  qmi_message_imsa_get_ims_services_status_output_unref(output);
}

void get_registration_state() {
  g_print(" - Attempting to read IMS registration status...\n");
  qmi_client_imsa_get_ims_registration_status(
      ctx->client, NULL, 10, ctx->cancellable,
      (GAsyncReadyCallback)get_ims_registration_status_ready, NULL);
}

void get_ims_services_state() {
  g_print(" - Attempting to read IMS services status...\n");
  qmi_client_imsa_get_ims_services_status(
      ctx->client, NULL, 10, ctx->cancellable,
      (GAsyncReadyCallback)get_ims_services_status_ready, NULL);
}

static void imsa_attempt_bind_finish(QmiClientImsa *client, GAsyncResult *res) {
  GError *error = NULL;
  QmiMessageImsaImsaBindSubscriptionOutput *output;
  g_print("%s: Going through\n", __func__);
  output = qmi_client_imsa_imsa_bind_subscription_finish(client, res, &error);
  if (!output) {
    g_print("FATAL: Output seems empty!\n");
    g_error_free(error);
    return;
  }

  if (!qmi_message_imsa_imsa_bind_subscription_output_get_result(output,
                                                                 &error)) {
    g_print("Err: IMSA bind failed: %s\n", error->message);
    g_error_free(error);
    qmi_message_imsa_imsa_bind_subscription_output_unref(output);
    return;
  }

  g_print("%s: Finish off by requesting status\n", __func__);
  ctx->sub_requested = 0;
}

void imsa_attempt_bind() {
  g_print(" - Attempting to bind to IMS...\n");
  QmiMessageImsaImsaBindSubscriptionInput *input;
  guint32 subscription_type = 0x00;
  input = qmi_message_imsa_imsa_bind_subscription_input_new();
  qmi_message_imsa_imsa_bind_subscription_input_set_subscription_type(
      input, subscription_type, NULL);
  qmi_client_imsa_imsa_bind_subscription(
      ctx->client, input, 30, ctx->cancellable,
      (GAsyncReadyCallback)imsa_attempt_bind_finish, NULL);
}

void imsa_start(QmiDevice *device, QmiClientImsa *client,
                GCancellable *cancellable) {

  /* Initialize context */
  ctx = g_slice_new(Context);
  ctx->device = g_object_ref(device);
  ctx->client = g_object_ref(client);
  ctx->cancellable = g_object_ref(cancellable);
  g_print("IMSA START\n");
  imsa_attempt_bind();
}