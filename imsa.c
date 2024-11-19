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
        return ctx->sub_requested;
}

static void get_ims_registration_status_ready(QmiClientImsa *client,
                                              GAsyncResult *res) {
  QmiMessageImsaGetImsRegistrationStatusOutput *output;
  QmiImsaImsRegistrationStatus registration_status;
  QmiImsaRegistrationTechnology registration_technology;
  GError *error = NULL;

  output =
      qmi_client_imsa_get_ims_registration_status_finish(client, res, &error);
  if (!output) {
    g_printerr("error: operation failed: %s\n", error->message);
    g_error_free(error);
    return;
  }

  if (!qmi_message_imsa_get_ims_registration_status_output_get_result(output,
                                                                      &error)) {
    g_printerr("error: couldn't get IMS registration status: %s\n",
               error->message);
    g_error_free(error);
    qmi_message_imsa_get_ims_registration_status_output_unref(output);
    return;
  }

  g_print("[%s] IMS registration:\n", qmi_device_get_path_display(ctx->device));

  if (qmi_message_imsa_get_ims_registration_status_output_get_ims_registration_status(
          output, &registration_status, NULL))
    g_print("\t    Status: '%s'\n",
            qmi_imsa_ims_registration_status_get_string(registration_status));

  if (qmi_message_imsa_get_ims_registration_status_output_get_ims_registration_technology(
          output, &registration_technology, NULL))
    g_print("\tTechnology: '%s'\n", qmi_imsa_registration_technology_get_string(
                                        registration_technology));

  qmi_message_imsa_get_ims_registration_status_output_unref(output);
}

static void get_ims_services_status_ready(QmiClientImsa *client,
                                          GAsyncResult *res) {
  QmiMessageImsaGetImsServicesStatusOutput *output;
  QmiImsaServiceStatus service_sms_status;
  QmiImsaRegistrationTechnology service_sms_technology;
  QmiImsaServiceStatus service_voice_status;
  QmiImsaRegistrationTechnology service_voice_technology;
  QmiImsaServiceStatus service_vt_status;
  QmiImsaRegistrationTechnology service_vt_technology;
  QmiImsaServiceStatus service_ut_status;
  QmiImsaRegistrationTechnology service_ut_technology;
  QmiImsaServiceStatus service_vs_status;
  QmiImsaRegistrationTechnology service_vs_technology;
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

  g_print("[%s] IMS services:\n", qmi_device_get_path_display(ctx->device));

  g_print("\tSMS service\n");

  if (qmi_message_imsa_get_ims_services_status_output_get_ims_sms_service_status(
          output, &service_sms_status, NULL))
    g_print("\t\t    Status: '%s'\n",
            qmi_imsa_service_status_get_string(service_sms_status));

  if (qmi_message_imsa_get_ims_services_status_output_get_ims_sms_service_registration_technology(
          output, &service_sms_technology, NULL))
    g_print(
        "\t\tTechnology: '%s'\n",
        qmi_imsa_registration_technology_get_string(service_sms_technology));

  g_print("\tVoice service\n");

  if (qmi_message_imsa_get_ims_services_status_output_get_ims_voice_service_status(
          output, &service_voice_status, NULL))
    g_print("\t\t    Status: '%s'\n",
            qmi_imsa_service_status_get_string(service_voice_status));

  if (qmi_message_imsa_get_ims_services_status_output_get_ims_voice_service_registration_technology(
          output, &service_voice_technology, NULL))
    g_print(
        "\t\tTechnology: '%s'\n",
        qmi_imsa_registration_technology_get_string(service_voice_technology));

  g_print("\tVideo Telephony service\n");

  if (qmi_message_imsa_get_ims_services_status_output_get_ims_video_telephony_service_status(
          output, &service_vt_status, NULL))
    g_print("\t\t    Status: '%s'\n",
            qmi_imsa_service_status_get_string(service_vt_status));

  if (qmi_message_imsa_get_ims_services_status_output_get_ims_video_telephony_service_registration_technology(
          output, &service_vt_technology, NULL))
    g_print("\t\tTechnology: '%s'\n",
            qmi_imsa_registration_technology_get_string(service_vt_technology));

  g_print("\tUE to TAS service\n");

  if (qmi_message_imsa_get_ims_services_status_output_get_ims_ue_to_tas_service_status(
          output, &service_ut_status, NULL))
    g_print("\t\t    Status: '%s'\n",
            qmi_imsa_service_status_get_string(service_ut_status));

  if (qmi_message_imsa_get_ims_services_status_output_get_ims_ue_to_tas_service_registration_technology(
          output, &service_ut_technology, NULL))
    g_print("\t\tTechnology: '%s'\n",
            qmi_imsa_registration_technology_get_string(service_ut_technology));

  g_print("\tVideo Share service\n");

  if (qmi_message_imsa_get_ims_services_status_output_get_ims_video_share_service_status(
          output, &service_vs_status, NULL))
    g_print("\t\t    Status: '%s'\n",
            qmi_imsa_service_status_get_string(service_vs_status));

  if (qmi_message_imsa_get_ims_services_status_output_get_ims_video_share_service_registration_technology(
          output, &service_vs_technology, NULL))
    g_print("\t\tTechnology: '%s'\n",
            qmi_imsa_registration_technology_get_string(service_vs_technology));

  qmi_message_imsa_get_ims_services_status_output_unref(output);
}

void get_registration_state() {
  g_print("Attempting to read IMS registration status...\n");
  qmi_client_imsa_get_ims_registration_status(
      ctx->client, NULL, 10, ctx->cancellable,
      (GAsyncReadyCallback)get_ims_registration_status_ready, NULL);
}

void get_ims_services_state() {
  g_print("Attempting to read IMS services status...\n");
  qmi_client_imsa_get_ims_services_status(
      ctx->client, NULL, 10, ctx->cancellable,
      (GAsyncReadyCallback)get_ims_services_status_ready, NULL);
}

void imsa_attempt_bind_finish(QmiClientImsa *client, GAsyncResult *res) {
    GError *error = NULL;
    g_print("BIND FINISH\n");
    QmiMessageImsaImsaBindSubscriptionOutput *output;
    output = qmi_client_imsa_imsa_bind_subscription_finish (client, res, &error);
    if (!output) {
        g_print("FATAL: Output seems empty!\n");
    }
    
    /*if (!output || qmi_message_imsa_imsa_bind_subscription_output_get_result(output, &error)) {
        g_print("Err: IMSA bind failed: %s\n", error->message);
    } */
    g_print("END %s\n", __func__);
}


void imsa_attempt_bind() {
  g_print("Attempting to bind to IMS...\n");
  QmiMessageImsaImsaBindSubscriptionInput *input;
  guint32 subscription_type = 0x00;
  ctx->sub_requested = 1;
  input = qmi_message_imsa_imsa_bind_subscription_input_new();
  qmi_message_imsa_imsa_bind_subscription_input_set_subscription_type(input, subscription_type, NULL);
  qmi_client_imsa_imsa_bind_subscription(
      ctx->client, input, 10, ctx->cancellable,
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
  get_registration_state();
  get_ims_services_state();
}