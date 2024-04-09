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
  QmiClientNas *client;
  GCancellable *cancellable;
} Context;

typedef struct {
    guint16 mcc;
    guint16 mnc;
} Carrier;

static Carrier *current_carrier;

static Context *ctx;


static void get_home_network_ready(QmiClientNas *client, GAsyncResult *res) {
  QmiMessageNasGetHomeNetworkOutput *output;
  GError *error = NULL;

  output = qmi_client_nas_get_home_network_finish(client, res, &error);
  if (!output) {
    g_printerr("error: operation failed: %s\n", error->message);
    g_error_free(error);
    //   operation_shutdown (FALSE);
    return;
  }

  if (!qmi_message_nas_get_home_network_output_get_result(output, &error)) {
    g_printerr("error: couldn't get home network: %s\n", error->message);
    g_error_free(error);
    qmi_message_nas_get_home_network_output_unref(output);
    //   operation_shutdown (FALSE);
    return;
  }

  g_print("[%s] Successfully got home network:\n",
          qmi_device_get_path_display(ctx->device));

  {
    guint16 mcc;
    guint16 mnc;
    const gchar *description;

    qmi_message_nas_get_home_network_output_get_home_network(
        output, &mcc, &mnc, &description, NULL);

    g_print("\tHome network:\n"
            "\t\tMCC: '%" G_GUINT16_FORMAT "'\n"
            "\t\tMNC: '%" G_GUINT16_FORMAT "'\n"
            "\t\tDescription: '%s'\n",
            mcc, mnc, description);
  }

  {
    QmiNasNetworkNameSource network_name_source;
    if (qmi_message_nas_get_home_network_output_get_network_name_source(
            output, &network_name_source, NULL)) {
      g_print("\tNetwork name source: %s\n",
              qmi_nas_network_name_source_get_string(network_name_source));
    }
  }

  {
    guint16 sid;
    guint16 nid;

    if (qmi_message_nas_get_home_network_output_get_home_system_id(
            output, &sid, &nid, NULL)) {
      g_print("\t\tSID: '%" G_GUINT16_FORMAT "'\n"
              "\t\tNID: '%" G_GUINT16_FORMAT "'\n",
              sid, nid);
    }
  }

  {
    guint16 mcc;
    guint16 mnc;
    QmiNasNetworkDescriptionEncoding description_encoding;
    GArray *description_array;

    if (qmi_message_nas_get_home_network_output_get_home_network_3gpp2_ext(
            output, &mcc, &mnc, NULL, /* display_description */
            &description_encoding, &description_array, NULL)) {
      g_autofree gchar *description = NULL;

      description = qmi_nas_read_string_from_network_description_encoded_array(
          description_encoding, description_array);
      g_print("\t3GPP2 Home network (extended):\n"
              "\t\tMCC: '%" G_GUINT16_FORMAT "'\n"
              "\t\tMNC: '%" G_GUINT16_FORMAT "'\n"
              "\t\tDescription: '%s'\n",
              mcc, mnc, description ?: "");
    }
  }

  qmi_message_nas_get_home_network_output_unref(output);
}

void get_home_network() {
  g_info("Asynchronously getting home network...");
  qmi_client_nas_get_home_network(ctx->client, NULL, 10, ctx->cancellable,
                                  (GAsyncReadyCallback)get_home_network_ready,
                                  NULL);
  return;
}

void nas_start(QmiDevice *device, QmiClientNas *client,
               GCancellable *cancellable) {

  /* Initialize context */
  ctx = g_slice_new(Context);
  ctx->device = g_object_ref(device);
  ctx->client = g_object_ref(client);
  ctx->cancellable = g_object_ref(cancellable);
  get_home_network();
}