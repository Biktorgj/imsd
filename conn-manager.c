/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Copyright (c) 2024, Biktorgj
 */
#include "conn-manager.h"
#include "wds.h"
#include <gio/gio.h>
#include <glib-unix.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <libqmi-glib.h>
#include <libqrtr-glib.h>

typedef struct {
  QmiDevice *device;
  QrtrBus *qrtr_bus;
  GCancellable *cancellable;
  QmiClient *wds_client;
  QmiClient *nas_client;
  QmiClient *imss_client;
  QmiClient *imsa_client;
  QmiClient *imsp_client;
  QmiClient *imsrtp_client;

} Context;
static Context *ctx;

QmiClient *get_client(QmiService service) {
  switch (service) {
  case QMI_SERVICE_NAS:
    return ctx->nas_client;
  case QMI_SERVICE_WDS:
    return ctx->wds_client;
  case QMI_SERVICE_IMS:
    return ctx->imss_client;
  case QMI_SERVICE_IMSA:
    return ctx->imsa_client;
  case QMI_SERVICE_IMSP:
    return ctx->imsp_client;
  case QMI_SERVICE_IMSRTP:
    return ctx->imsrtp_client;
  default:
    break;
  }
  g_assert_not_reached();
}

void cancel_connection_manager() {
  g_printerr("Cancel connmanager\n");
  if (ctx->cancellable) {
    /* Ignore consecutive requests of cancellation */
    if (!g_cancellable_is_cancelled(ctx->cancellable)) {
      g_printerr("Stopping Connection Manager...\n");
      release_clients();
      g_cancellable_cancel(ctx->cancellable);
      /* Re-set the signal handler to allow main loop cancellation on
       * second signal */
    }
  }
}

static void close_ready(QmiDevice *dev, GAsyncResult *res) {
  GError *error = NULL;

  if (!qmi_device_close_finish(dev, res, &error)) {
    g_printerr("error: couldn't close: %s\n", error->message);
    g_error_free(error);
  } else
    g_info("Device Closed");

  g_info("Ready to exit main\n");
}

static void release_client_ready(QmiDevice *dev, GAsyncResult *res) {
  GError *error = NULL;
  if (!qmi_device_release_client_finish(dev, res, &error)) {
    g_printerr("error: couldn't release client: %s\n", error->message);
    g_error_free(error);
  } else {
    g_printerr("Client released!\n");
  }
  // TODO: Not working
  if (!ctx->wds_client) {
    g_printerr("WDS released\n");
  }
  if (!ctx->wds_client && !ctx->nas_client && !ctx->imss_client && !ctx->imsa_client &&
      !ctx->imsp_client && !ctx->imsrtp_client) {
    qmi_device_close_async(dev, 10, NULL, (GAsyncReadyCallback)close_ready,
                           NULL);
  } else {
    g_printerr("Can't close yet, some clients are still allocated\n");
  }
}

void release_clients() {
  /* Cleanup cancellation */
  g_clear_object(&ctx->cancellable);

  if (ctx->wds_client)
    qmi_device_release_client(ctx->device, ctx->wds_client,
                              QMI_DEVICE_RELEASE_CLIENT_FLAGS_NONE, 10, NULL,
                              (GAsyncReadyCallback)release_client_ready, NULL);
  if (ctx->nas_client)
    qmi_device_release_client(ctx->device, ctx->nas_client,
                              QMI_DEVICE_RELEASE_CLIENT_FLAGS_NONE, 10, NULL,
                              (GAsyncReadyCallback)release_client_ready, NULL);
  if (ctx->imss_client)
    qmi_device_release_client(ctx->device, ctx->imss_client,
                              QMI_DEVICE_RELEASE_CLIENT_FLAGS_NONE, 10, NULL,
                              (GAsyncReadyCallback)release_client_ready, NULL);
  if (ctx->imsa_client)
    qmi_device_release_client(ctx->device, ctx->imsa_client,
                              QMI_DEVICE_RELEASE_CLIENT_FLAGS_NONE, 10, NULL,
                              (GAsyncReadyCallback)release_client_ready, NULL);
  if (ctx->imsp_client)
    qmi_device_release_client(ctx->device, ctx->imsp_client,
                              QMI_DEVICE_RELEASE_CLIENT_FLAGS_NONE, 10, NULL,
                              (GAsyncReadyCallback)release_client_ready, NULL);
  if (ctx->imsrtp_client)
    qmi_device_release_client(ctx->device, ctx->imsrtp_client,
                              QMI_DEVICE_RELEASE_CLIENT_FLAGS_NONE, 10, NULL,
                              (GAsyncReadyCallback)release_client_ready, NULL);
}

static void wds_allocate_client_ready(QmiDevice *dev, GAsyncResult *res) {
  GError *error = NULL;
  ctx->wds_client = qmi_device_allocate_client_finish(dev, res, &error);
  if (!ctx->wds_client) {
    g_printerr("error: couldn't create client for the WDSS service: %s\n",
               error->message);
    exit(EXIT_FAILURE);
  }
  g_printerr("WDS Allocated!\n");
  wds_start(dev, QMI_CLIENT_WDS(ctx->wds_client), ctx->cancellable);
}

static void imss_allocate_client_ready(QmiDevice *dev, GAsyncResult *res) {
  GError *error = NULL;
  ctx->imss_client = qmi_device_allocate_client_finish(dev, res, &error);
  if (!ctx->imss_client) {
    g_printerr("error: couldn't create client for the IMSS service: %s\n",
               error->message);
    exit(EXIT_FAILURE);
  }

  g_printerr("IMSS Allocated!\n");
}

static void imsp_allocate_client_ready(QmiDevice *dev, GAsyncResult *res) {
  GError *error = NULL;
  ctx->imsp_client = qmi_device_allocate_client_finish(dev, res, &error);
  if (!ctx->imsp_client) {
    g_printerr(
        "error: couldn't create client for the IMS Presenece service: %s\n",
        error->message);
    // exit(EXIT_FAILURE);
    return;
  }

  g_printerr("IMS Presence Allocated!\n");
}

static void device_open_ready(QmiDevice *dev, GAsyncResult *res) {
  GError *error = NULL;

  if (!qmi_device_open_finish(dev, res, &error)) {
    g_printerr("error: couldn't open the QmiDevice: %s\n", error->message);
    exit(EXIT_FAILURE);
  }

  g_info("QMI Device at '%s' ready", qmi_device_get_path_display(dev));
  /* We allocate all of our clients at once */
  qmi_device_allocate_client(
      dev, QMI_SERVICE_WDS, QMI_CID_NONE, 10, ctx->cancellable,
      (GAsyncReadyCallback)wds_allocate_client_ready, NULL);

  qmi_device_allocate_client(
      dev, QMI_SERVICE_IMS, QMI_CID_NONE, 10, ctx->cancellable,
      (GAsyncReadyCallback)imss_allocate_client_ready, NULL);

  qmi_device_allocate_client(
      dev, QMI_SERVICE_IMSP, QMI_CID_NONE, 10, ctx->cancellable,
      (GAsyncReadyCallback)imsp_allocate_client_ready, NULL);
}

static void qmi_device_ready(GObject *unused, GAsyncResult *res) {
  QmiDeviceOpenFlags open_flags = QMI_DEVICE_OPEN_FLAGS_NONE;
  GError *error = NULL;

  ctx->device = qmi_device_new_finish(res, &error);
  if (!ctx->device) {
    g_printerr("error: couldn't create QmiDevice: %s\n", error->message);
    exit(EXIT_FAILURE);
  }
  /* Setup device open flags */
  open_flags |= QMI_DEVICE_OPEN_FLAGS_EXPECT_INDICATIONS;

  /* Open the device */
  qmi_device_open(ctx->device, open_flags, 15, ctx->cancellable,
                  (GAsyncReadyCallback)device_open_ready, NULL);
}

static void bus_new_callback(GObject *source, GAsyncResult *res,
                             gpointer user_data) {
  g_autoptr(GError) error = NULL;
  guint node_id;
  QrtrNode *node;

  node_id = GPOINTER_TO_UINT(user_data);

  ctx->qrtr_bus = qrtr_bus_new_finish(res, &error);
  if (!ctx->qrtr_bus) {
    g_printerr("error: couldn't access QRTR bus: %s\n", error->message);
    exit(EXIT_FAILURE);
  }

  node = qrtr_bus_peek_node(ctx->qrtr_bus, node_id);
  if (!node) {
    g_printerr("error: node with id %u not found in QRTR bus\n", node_id);
    exit(EXIT_FAILURE);
  }

  qmi_device_new_from_node(node, ctx->cancellable,
                           (GAsyncReadyCallback)qmi_device_ready, NULL);
}

gboolean create_client_connection(GFile *file) {
  g_autofree gchar *fd = NULL;
  ctx = g_slice_new(Context);

  ctx->cancellable = g_cancellable_new();

  fd = g_file_get_path(file);
  if (fd) {
    g_info("Connecting via device node");
    qmi_device_new(file, ctx->cancellable,
                   (GAsyncReadyCallback)qmi_device_ready, NULL);
    return TRUE;
  } else {
    g_info("Connecting via QRTR");
    guint32 node_id;
    fd = g_file_get_uri(file);
    if (qrtr_get_node_for_uri(fd, &node_id)) {
      qrtr_bus_new(1000, ctx->cancellable,
                   (GAsyncReadyCallback)bus_new_callback,
                   GUINT_TO_POINTER(node_id));
      return TRUE;
    }
    g_printerr("Failed to connect via QRTR");
    return FALSE;
  }
  g_printerr("Failed to determine the device type");
  return FALSE;
}