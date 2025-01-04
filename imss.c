/* SPDX-License-Identifier: GPL-3.0-or-later */


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
    g_printerr("[IMSS] %s: Service enable result was an error: %s\n", __func__,
               error->message);
    g_error_free(error);
    qmi_message_ims_set_ims_service_enable_config_output_unref(output);
    return;
  }

  g_print("[IMSS] Start IMS Services finished!\n");
}

void attempt_start_imss_services() {
  QmiMessageImsSetImsServiceEnableConfigInput *input;
  GError *error = NULL;

  g_info("[IMSS] Service Enable Config: Start \n");
  input = qmi_message_ims_set_ims_service_enable_config_input_new();
  qmi_message_ims_set_ims_service_enable_config_input_set_volte_status(input, 1,
                                                                       &error);
  qmi_message_ims_set_ims_service_enable_config_input_set__rtt_service_status_(
      input, 1, &error);
  qmi_message_ims_set_ims_service_enable_config_input_set_call_mode_preference_roaming_service_status(
      input, 1, &error);
  qmi_message_ims_set_ims_service_enable_config_input_set_rcs_messaging_service_status(
      input, 1, &error);
  qmi_message_ims_set_ims_service_enable_config_input_set_xdm_client_status(
      input, 1, &error);
  qmi_message_ims_set_ims_service_enable_config_input_set_autoconfig_service_status(
      input, 0, &error);
  qmi_message_ims_set_ims_service_enable_config_input_set_presence_service_status(
      input, 1, &error);
  qmi_message_ims_set_ims_service_enable_config_input_set_ims_service_status(
      input, 1, &error);
  qmi_message_ims_set_ims_service_enable_config_input_set_enable_wifi_calling_support_in_roaming_through_client_provisioning(
      input, 0, &error);
  qmi_client_ims_set_ims_service_enable_config(
      ctx->client, input, 10, ctx->cancellable,
      (GAsyncReadyCallback)attempt_start_imss_services_ready, NULL);
}
static void qmi_qualcomm_ip_call_callback(QmiClientIms *client,
                                          GAsyncResult *res) {
  QmiMessageImsSetQualcommIpCallConfigOutput *output;
  GError *error = NULL;
  g_info("********* QIPCALL READY RESPONSE\n");
  output =
      qmi_client_ims_set_qualcomm_ip_call_config_finish(client, res, &error);
  if (!output) {
    g_printerr("error: operation failed: %s\n", error->message);
    g_error_free(error);
    return;
  }

  if (!qmi_message_ims_set_qualcomm_ip_call_config_output_get_result(output,
                                                                     &error)) {
    g_printerr("QIPCALL: It told me to fuck off %s\n", error->message);
    g_error_free(error);
    qmi_message_ims_set_qualcomm_ip_call_config_output_unref(output);
    return;
  }
}

void imss_start_qualcomm_ip_call_settings() {
  QmiMessageImsSetQualcommIpCallConfigInput *input;
  GError *error = NULL;

  g_info("Trying to start IMS service with our own stuff!\n");
  input = qmi_message_ims_set_qualcomm_ip_call_config_input_new();
  qmi_message_ims_set_qualcomm_ip_call_config_input_set_volte_enabled(input, 1,
                                                                      &error);
  qmi_message_ims_set_qualcomm_ip_call_config_input_set_mobile_data_enabled(
      input, 1, &error);
  qmi_message_ims_set_qualcomm_ip_call_config_input_set_vt_call_enabled(
      input, 1, &error);
  qmi_client_ims_set_qualcomm_ip_call_config(
      ctx->client, input, 10, ctx->cancellable,
      (GAsyncReadyCallback)qmi_qualcomm_ip_call_callback, NULL);
}

static void ims_attempt_bind_finish(QmiClientIms *client, GAsyncResult *res) {
  GError *error = NULL;
  QmiMessageImsBindOutput *output;
  g_print("%s: Going through\n", __func__);
  output = qmi_client_ims_bind_finish(client, res, &error);
  if (!output) {
    g_print("FATAL: Output seems empty!\n");
    g_error_free(error);
    return;
  }

  if (!qmi_message_ims_bind_output_get_result(output, &error)) {
    g_print("Err: IMS bind failed: %s\n", error->message);
    g_error_free(error);
    qmi_message_ims_bind_output_unref(output);
    return;
  }

  g_print("[IMSS] %s: Bind Finished. Requesting IMS Service Start\n", __func__);
  attempt_start_imss_services();
}

void imss_attempt_bind() {
  g_print("[IMSS]: Attempting to bind to IMS Service...\n");
  QmiMessageImsBindInput *input;
  guint32 subscription_type = 0x00; // Primary == 0x00 || Secondary == 0x01, ||
                                    // tertiary == 0x02 || None 0xFFFFFFFF
  input = qmi_message_ims_bind_input_new();
  qmi_message_ims_bind_input_set_binding(input, subscription_type, NULL);
  qmi_client_ims_bind(ctx->client, input, 10, ctx->cancellable,
                      (GAsyncReadyCallback)ims_attempt_bind_finish, NULL);
}

static void imss_get_ims_ua_finish(QmiClientIms *client, GAsyncResult *res) {
  GError *error = NULL;
  QmiMessageImsGetImsConfigOutput *output;
  const gchar *user_agent = NULL;
  g_print("%s: Start\n", __func__);
  output = qmi_client_ims_get_ims_config_finish(client, res, &error);
  if (!output) {
    g_print("FATAL: Output seems empty!\n");
    g_error_free(error);
    return;
  }

  if (!qmi_message_ims_get_ims_config_output_get_result(output, &error)) {
    g_print("Err: IMS Get User Agent failed: %s\n", error->message);
    g_error_free(error);
    qmi_message_ims_get_ims_config_output_unref(output);
    return;
  }

  if (qmi_message_ims_get_ims_config_output_get_current_ims_user_agent(
          output, &user_agent, NULL))
    g_print("--> IMS User Agent '%s'\n", user_agent);
  else {
    g_print("--> IMS User Agent is empty!\n");
  }
  g_print("[IMSS] %s: Get IMS Config finish\n", __func__);
}

void imss_get_ims_ua() {
  g_print("[IMSS]: Get User Agent...\n");
  qmi_client_ims_get_ims_config(ctx->client, NULL, 10, ctx->cancellable,
                                (GAsyncReadyCallback)imss_get_ims_ua_finish,
                                NULL);
}

static void imss_set_ua_done(QmiClientIms *client, GAsyncResult *res) {
  GError *error = NULL;
  QmiMessageImsSetImsConfigOutput *output;
  g_print("%s: Start\n", __func__);
  output = qmi_client_ims_set_ims_config_finish(client, res, &error);
  if (!output) {
    g_print("%s: FATAL: Output seems empty!\n", __func__);
    g_error_free(error);
    return;
  }

  if (!qmi_message_ims_set_ims_config_output_get_result(output, &error)) {
    g_print("Err: IMS Set User Agent failed: %s\n", error->message);
    g_error_free(error);
    qmi_message_ims_set_ims_config_output_unref(output);
    return;
  }

  g_print("[IMSS] %s: Set IMS Config finish\n", __func__);
}

void imss_set_user_agent() {
  g_print("Attempting to set the User Agent...\n");
  QmiMessageImsSetImsConfigInput *input;
  input = qmi_message_ims_set_ims_config_input_new();
  qmi_message_ims_set_ims_config_input_set_user_agent(input, "Test UA 01Bik",
                                                      NULL);
  qmi_client_ims_set_ims_config(ctx->client, input, 10, ctx->cancellable,
                                (GAsyncReadyCallback)imss_set_ua_done, NULL);
}

void imss_start(QmiDevice *device, QmiClientIms *client,
                GCancellable *cancellable) {

  /* Initialize context */
  ctx = g_slice_new(Context);
  ctx->device = g_object_ref(device);
  ctx->client = g_object_ref(client);
  ctx->cancellable = g_object_ref(cancellable);
  imss_attempt_bind();
//  imss_set_user_agent();
//  imss_get_ims_ua();
}