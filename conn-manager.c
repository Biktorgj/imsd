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

static QmiDevice *device;
static QrtrBus *qrtr_bus;
static gboolean dev_type;
static GCancellable *conn_cancellable;

QmiClient *wds_client;
QmiClient *imss_client;
QmiClient *imsa_client;
QmiClient *imsp_client;

void cancel_connection_manager() {
  g_printerr("Cancel connmanager\n");
  if (conn_cancellable) {
    /* Ignore consecutive requests of cancellation */
    if (!g_cancellable_is_cancelled(conn_cancellable)) {
      g_printerr("Stopping Connection Manager...\n");
      g_cancellable_cancel(conn_cancellable);
      /* Re-set the signal handler to allow main loop cancellation on
       * second signal */
    }
  }
}

static void wds_allocate_client_ready(QmiDevice *dev, GAsyncResult *res) {
  GError *error = NULL;
  wds_client = qmi_device_allocate_client_finish(dev, res, &error);
  if (!wds_client) {
    g_printerr("error: couldn't create client for the WDSS service: %s\n", error->message);
    exit(EXIT_FAILURE);
  }
  g_printerr("WDS Allocated!\n");

}

static void imss_allocate_client_ready(QmiDevice *dev, GAsyncResult *res) {
  GError *error = NULL;
  imss_client = qmi_device_allocate_client_finish(dev, res, &error);
  if (!imss_client) {
    g_printerr("error: couldn't create client for the IMSS service: %s\n", error->message);
    exit(EXIT_FAILURE);
  }

  g_printerr("IMSS Allocated!\n");
}

static void device_open_ready(QmiDevice *dev, GAsyncResult *res) {
  GError *error = NULL;

  if (!qmi_device_open_finish(dev, res, &error)) {
    g_printerr("error: couldn't open the QmiDevice: %s\n", error->message);
    exit(EXIT_FAILURE);
  }

  g_info("QMI Device at '%s' ready", qmi_device_get_path_display(dev));
  /* We allocate all of our clients at once */
  qmi_device_allocate_client(dev, QMI_SERVICE_WDS, QMI_CID_NONE, 10, conn_cancellable,
                             (GAsyncReadyCallback)wds_allocate_client_ready, NULL);
 
  qmi_device_allocate_client(dev, QMI_SERVICE_IMS, QMI_CID_NONE, 10, conn_cancellable,
                             (GAsyncReadyCallback)imss_allocate_client_ready, NULL);
 
}

static void qmi_device_ready(GObject *unused, GAsyncResult *res) {
  QmiDeviceOpenFlags open_flags = QMI_DEVICE_OPEN_FLAGS_NONE;
  GError *error = NULL;

  device = qmi_device_new_finish(res, &error);
  if (!device) {
    g_printerr("error: couldn't create QmiDevice: %s\n", error->message);
    exit(EXIT_FAILURE);
  }
  /* Setup device open flags */
  open_flags |= QMI_DEVICE_OPEN_FLAGS_EXPECT_INDICATIONS;

  /* Open the device */
  qmi_device_open(device, open_flags, 15, conn_cancellable,
                  (GAsyncReadyCallback)device_open_ready, NULL);
}

static void bus_new_callback(GObject *source, GAsyncResult *res,
                             gpointer user_data) {
  g_autoptr(GError) error = NULL;
  guint node_id;
  QrtrNode *node;

  node_id = GPOINTER_TO_UINT(user_data);

  qrtr_bus = qrtr_bus_new_finish(res, &error);
  if (!qrtr_bus) {
    g_printerr("error: couldn't access QRTR bus: %s\n", error->message);
    exit(EXIT_FAILURE);
  }

  node = qrtr_bus_peek_node(qrtr_bus, node_id);
  if (!node) {
    g_printerr("error: node with id %u not found in QRTR bus\n", node_id);
    exit(EXIT_FAILURE);
  }

  qmi_device_new_from_node(node, conn_cancellable,
                           (GAsyncReadyCallback)qmi_device_ready, NULL);
}

gboolean create_client_connection(GFile *file) {
  dev_type = 0;
  g_autofree gchar *fd = NULL;

  conn_cancellable = g_cancellable_new();

  fd = g_file_get_path(file);
  if (fd) {
    dev_type = 1;
    g_info("Connecting via device node");
    qmi_device_new(file, conn_cancellable,
                   (GAsyncReadyCallback)qmi_device_ready, NULL);
    return TRUE;
  } else {
    g_info("Connecting via QRTR");
    dev_type = 2;
    guint32 node_id;
    fd = g_file_get_uri(file);
    if (qrtr_get_node_for_uri(fd, &node_id)) {
      qrtr_bus_new(1000, conn_cancellable,
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