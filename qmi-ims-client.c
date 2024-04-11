/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Copyright (c) 2024, Biktorgj
 */
#include "qmi-ims-client.h"
#include "wds.h"
#include "nas.h"
#include <gio/gio.h>
#include <glib-unix.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <libqmi-glib.h>
#include <libqrtr-glib.h>

/* Since we are allocating the clients here
   it makes sense to use this as a hub that 
   triggers the start of everything else
   
   I'll probably move things around a bit more
   before I settle with a layout but... 
   */
typedef struct {
  gboolean allocated;
  QmiClient *client;
} Client;

typedef struct {
  QmiDevice *device;
  QrtrBus *qrtr_bus;
  GCancellable *cancellable;
  Client wds;
  Client nas;
  Client imss;
  Client imsa;
  Client imsp;
  Client imsrtp;
} Context;
static Context *ctx;

QmiClient *get_client(QmiService service) {
  switch (service) {
  case QMI_SERVICE_NAS:
    return ctx->nas.client;
  case QMI_SERVICE_WDS:
    return ctx->wds.client;
  case QMI_SERVICE_IMS:
    return ctx->imss.client;
  case QMI_SERVICE_IMSA:
    return ctx->imsa.client;
  case QMI_SERVICE_IMSP:
    return ctx->imsp.client;
  case QMI_SERVICE_IMSRTP:
    return ctx->imsrtp.client;
  default:
    break;
  }
  g_assert_not_reached();
}

void cancel_connection_manager() {
  g_printerr("Stopping connmanager\n");
  release_clients();
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

static void release_client_ready(QmiDevice *dev, GAsyncResult *res,
                                 gpointer *client) {
  GError *error = NULL;
  if (!qmi_device_release_client_finish(dev, res, &error)) {
    g_printerr("error: couldn't release client: %s\n", error->message);
    g_error_free(error);
  } else {
    g_printerr("Client released!\n");
  }
  client = FALSE;

  if (!ctx->wds.allocated && !ctx->nas.allocated && !ctx->imss.allocated &&
      !ctx->imsa.allocated && !ctx->imsp.allocated && !ctx->imsrtp.allocated) {
    g_printerr("Closing device\n");
    qmi_device_close_async(dev, 1, NULL, (GAsyncReadyCallback)close_ready,
                           NULL);
  } else {
    g_printerr("Can't close yet, some clients are still allocated\n");
  }
}

void release_clients() {
  g_printerr("Releasing clients\n");
  if (ctx->wds.allocated) {
    g_printerr("* WDS\n");
    qmi_device_release_client(
        ctx->device, ctx->wds.client, QMI_DEVICE_RELEASE_CLIENT_FLAGS_NONE, 10,
        NULL, (GAsyncReadyCallback)release_client_ready, &ctx->wds.allocated);
  }
  if (ctx->nas.allocated) {
    g_printerr("* NAS\n");
    qmi_device_release_client(
        ctx->device, ctx->nas.client, QMI_DEVICE_RELEASE_CLIENT_FLAGS_NONE, 10,
        NULL, (GAsyncReadyCallback)release_client_ready, &ctx->nas.allocated);
  }
  if (ctx->imss.allocated) {
    g_printerr("* IMSS\n");
    qmi_device_release_client(
        ctx->device, ctx->imss.client, QMI_DEVICE_RELEASE_CLIENT_FLAGS_NONE, 10,
        NULL, (GAsyncReadyCallback)release_client_ready, &ctx->imss.allocated);
  }
  if (ctx->imsa.allocated) {
    g_printerr("* IMSA\n");
    qmi_device_release_client(
        ctx->device, ctx->imsa.client, QMI_DEVICE_RELEASE_CLIENT_FLAGS_NONE, 10,
        NULL, (GAsyncReadyCallback)release_client_ready, &ctx->imsa.allocated);
  }
  if (ctx->imsp.allocated) {
    g_printerr("* IMSP\n");
    qmi_device_release_client(
        ctx->device, ctx->imsp.client, QMI_DEVICE_RELEASE_CLIENT_FLAGS_NONE, 10,
        NULL, (GAsyncReadyCallback)release_client_ready, &ctx->imsp.allocated);
  }
  if (ctx->imsrtp.allocated) {
    g_printerr("* IMS RTP\n");
    qmi_device_release_client(ctx->device, ctx->imsrtp.client,
                              QMI_DEVICE_RELEASE_CLIENT_FLAGS_NONE, 10, NULL,
                              (GAsyncReadyCallback)release_client_ready,
                              &ctx->imsrtp.allocated);
  }
}

static void wds_allocate_client_ready(QmiDevice *dev, GAsyncResult *res) {
  GError *error = NULL;
  ctx->wds.client = qmi_device_allocate_client_finish(dev, res, &error);
  if (!ctx->wds.client) {
    g_printerr("error: couldn't create client for the WDSS service: %s\n",
               error->message);
    exit(EXIT_FAILURE);
  }
  g_printerr("WDS Allocated!\n");
  ctx->wds.allocated = 1;
  wds_start(dev, QMI_CLIENT_WDS(ctx->wds.client), ctx->cancellable);
}

static void imss_allocate_client_ready(QmiDevice *dev, GAsyncResult *res) {
  GError *error = NULL;
  ctx->imss.client = qmi_device_allocate_client_finish(dev, res, &error);
  if (!ctx->imss.client) {
    g_printerr("error: couldn't create client for the IMSS service: %s\n",
               error->message);
    exit(EXIT_FAILURE);
  }
  ctx->imss.allocated = 1;
  g_printerr("IMSS Allocated!\n");
}

static void imsp_allocate_client_ready(QmiDevice *dev, GAsyncResult *res) {
  GError *error = NULL;
  ctx->imsp.client = qmi_device_allocate_client_finish(dev, res, &error);
  if (!ctx->imsp.client) {
    g_printerr(
        "error: couldn't create client for the IMS Presenece service: %s\n",
        error->message);
    // exit(EXIT_FAILURE);
    return;
  }
  ctx->imsp.allocated = 1;
  g_printerr("IMS Presence Allocated!\n");
}

static void nas_allocate_client_ready(QmiDevice *dev, GAsyncResult *res) {
  GError *error = NULL;
  ctx->nas.client = qmi_device_allocate_client_finish(dev, res, &error);
  if (!ctx->nas.client) {
    g_printerr("error: couldn't create client for the NAS service: %s\n",
               error->message);
    exit(EXIT_FAILURE);
  }
  ctx->nas.allocated = 1;
  g_printerr("NAS Allocated!\n");
  nas_start(dev, QMI_CLIENT_NAS(ctx->nas.client), ctx->cancellable);

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
      dev, QMI_SERVICE_NAS, QMI_CID_NONE, 10, ctx->cancellable,
      (GAsyncReadyCallback)nas_allocate_client_ready, NULL);

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

gboolean create_qmi_client_connection(GFile *file, GCancellable *cancellable) {
  g_autofree gchar *fd = NULL;
  ctx = g_slice_new(Context);
  ctx->cancellable = g_object_ref(cancellable);
  ctx->wds.allocated = 0;
  ctx->nas.allocated = 0;
  ctx->imss.allocated = 0;
  ctx->imsa.allocated = 0;
  ctx->imsrtp.allocated = 0;
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