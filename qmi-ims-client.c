/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Copyright (c) 2024, Biktorgj
 */
#include "qmi-ims-client.h"
#include "imss.h"
#include "imsa.h"
#include "nas.h"
#include "wds.h"
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
  QmiDevice *device;
  QrtrBus *qrtr_bus;
  GCancellable *cancellable;
  QmiClient *wds;
  QmiClient *nas;
  QmiClient *imss;
  QmiClient *imsa;
  QmiClient *imsp;
  QmiClient *imsrtp;
} Context;
static Context *ctx;

typedef struct _IMSD_runcfg {
  gboolean status;
  Carrier carrier_data;
  guint apn_status;
  gchar *curr_apn;
  guint wds_ready;
  guint nas_ready;
  guint imss_ready;
  guint imsa_ready;
  guint imsp_ready;
  guint imsrtp_ready;
  gboolean is_initialized;
  gboolean exit_requested;
} IMSD_runtime;

static IMSD_runtime *runtime;

/* Helpers to (de) allocate services */

static gboolean close_device(gpointer userdata);
gboolean wait_for_init(void *data);

void cancel_connection_manager() {
  g_printerr("Stopping connmanager\n");
  wds_do_stop_network(TRUE);
  release_clients();
  /* What an ugly hack :) */
  g_timeout_add(0, close_device, NULL);
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

static gboolean close_device(gpointer userdata) {
  do {
    g_printerr("Waiting for all clients to finish being released...\n");
    if (ctx->wds)
      g_printerr("WDS still allocated\n");
    if (ctx->nas)
      g_printerr("NAS still allocated\n");
    if (ctx->imss)
      g_printerr("IMSS still allocated\n");
    if (ctx->imsa)
      g_printerr("IMSA still allocated\n");
    if (ctx->imsp)
      g_printerr("IMSP still allocated\n");
    if (ctx->imsrtp)
      g_printerr("IMSRTP still allocated\n");
    sleep(1);
  } while (ctx->wds || ctx->nas || ctx->imss || ctx->imsa || ctx->imsp ||
           ctx->imsrtp);

  g_printerr("Closing device\n");
  qmi_device_close_async(ctx->device, 1, NULL, (GAsyncReadyCallback)close_ready,
                         NULL);

  return FALSE;
}

static void wds_release_client_ready(QmiDevice *dev, GAsyncResult *res) {
  GError *error = NULL;
  if (!qmi_device_release_client_finish(dev, res, &error)) {
    g_printerr("error: couldn't release WDS client: %s\n", error->message);
    g_error_free(error);
  } else {
    g_printerr("WDS Client released!\n");
    ctx->wds = NULL;
  }
}

static void nas_release_client_ready(QmiDevice *dev, GAsyncResult *res) {
  GError *error = NULL;
  if (!qmi_device_release_client_finish(dev, res, &error)) {
    g_printerr("error: couldn't release NAS client: %s\n", error->message);
    g_error_free(error);
  } else {
    g_printerr("NAS Client released!\n");
    ctx->nas = NULL;
  }
}

static void imss_release_client_ready(QmiDevice *dev, GAsyncResult *res) {
  GError *error = NULL;
  if (!qmi_device_release_client_finish(dev, res, &error)) {
    g_printerr("error: couldn't release IMSS client: %s\n", error->message);
    g_error_free(error);
  } else {
    g_printerr("IMSS Client released!\n");
    ctx->imss = NULL;
  }
}

static void imsa_release_client_ready(QmiDevice *dev, GAsyncResult *res) {
  GError *error = NULL;
  if (!qmi_device_release_client_finish(dev, res, &error)) {
    g_printerr("error: couldn't release IMSA client: %s\n", error->message);
    g_error_free(error);
  } else {
    g_printerr("IMSA Client released!\n");
    ctx->imsa = NULL;
  }
}

static void imsp_release_client_ready(QmiDevice *dev, GAsyncResult *res) {
  GError *error = NULL;
  if (!qmi_device_release_client_finish(dev, res, &error)) {
    g_printerr("error: couldn't release IMSP client: %s\n", error->message);
    g_error_free(error);
  } else {
    g_printerr("Client released!\n");
    ctx->imsp = NULL;
  }
}

static void imsrtp_release_client_ready(QmiDevice *dev, GAsyncResult *res) {
  GError *error = NULL;
  if (!qmi_device_release_client_finish(dev, res, &error)) {
    g_printerr("error: couldn't release IMS RTP client: %s\n", error->message);
    g_error_free(error);
  } else {
    g_printerr("Client released!\n");
    ctx->imsrtp = NULL;
  }
}

void release_clients() {
  g_printerr("Releasing clients:\n");
  if (ctx->wds) {
    g_printerr("* WDS\n");
    qmi_device_release_client(
        ctx->device, ctx->wds, QMI_DEVICE_RELEASE_CLIENT_FLAGS_NONE, 1, NULL,
        (GAsyncReadyCallback)wds_release_client_ready, NULL);
  }
  if (ctx->nas) {
    g_printerr("* NAS\n");
    qmi_device_release_client(
        ctx->device, ctx->nas, QMI_DEVICE_RELEASE_CLIENT_FLAGS_NONE, 1, NULL,
        (GAsyncReadyCallback)nas_release_client_ready, NULL);
  }
  if (ctx->imss) {
    g_printerr("* IMSS\n");
    qmi_device_release_client(
        ctx->device, ctx->imss, QMI_DEVICE_RELEASE_CLIENT_FLAGS_NONE, 1, NULL,
        (GAsyncReadyCallback)imss_release_client_ready, NULL);
  }
  if (ctx->imsa) {
    g_printerr("* IMSA\n");
    qmi_device_release_client(
        ctx->device, ctx->imsa, QMI_DEVICE_RELEASE_CLIENT_FLAGS_NONE, 1, NULL,
        (GAsyncReadyCallback)imsa_release_client_ready, NULL);
  }
  if (ctx->imsp) {
    g_printerr("* IMSP\n");
    qmi_device_release_client(
        ctx->device, ctx->imsp, QMI_DEVICE_RELEASE_CLIENT_FLAGS_NONE, 1, NULL,
        (GAsyncReadyCallback)imsp_release_client_ready, NULL);
  }
  if (ctx->imsrtp) {
    g_printerr("* IMS RTP\n");
    qmi_device_release_client(
        ctx->device, ctx->imsrtp, QMI_DEVICE_RELEASE_CLIENT_FLAGS_NONE, 10,
        NULL, (GAsyncReadyCallback)imsrtp_release_client_ready, NULL);
  }
}

static void wds_allocate_client_ready(QmiDevice *dev, GAsyncResult *res) {
  GError *error = NULL;
  ctx->wds = qmi_device_allocate_client_finish(dev, res, &error);
  if (!ctx->wds) {
    runtime->wds_ready = IMS_INIT_ERR;

    g_printerr("error: couldn't create client for the WDSS service: %s\n",
               error->message);
    exit(EXIT_FAILURE);
  }
  runtime->wds_ready = IMS_INIT_OK;
  g_printerr("WDS Allocated!\n");
  wds_start(dev, QMI_CLIENT_WDS(ctx->wds), ctx->cancellable);
}

static void imss_allocate_client_ready(QmiDevice *dev, GAsyncResult *res) {
  GError *error = NULL;
  ctx->imss = qmi_device_allocate_client_finish(dev, res, &error);
  if (!ctx->imss) {
    runtime->imss_ready = IMS_INIT_ERR;
    g_printerr("error: couldn't create client for the IMSS service: %s\n",
               error->message);
    exit(EXIT_FAILURE);
  }
  runtime->imss_ready = IMS_INIT_OK;
  g_printerr("IMSS Allocated!\n");
  imss_start(dev, QMI_CLIENT_IMS(ctx->imss), ctx->cancellable);
}

static void imsp_allocate_client_ready(QmiDevice *dev, GAsyncResult *res) {
  GError *error = NULL;
  ctx->imsp = qmi_device_allocate_client_finish(dev, res, &error);
  if (!ctx->imsp) {
    runtime->imsp_ready = IMS_INIT_ERR;

    g_printerr(
        "error: couldn't create client for the IMS Presenece service: %s\n",
        error->message);
    return;
  }
  runtime->imsp_ready = IMS_INIT_OK;
  g_printerr("IMS Presence Allocated!\n");
}

static void imsrtp_allocate_client_ready(QmiDevice *dev, GAsyncResult *res) {
  GError *error = NULL;
  ctx->imsrtp = qmi_device_allocate_client_finish(dev, res, &error);
  if (!ctx->imsrtp) {
    runtime->imsrtp_ready = IMS_INIT_ERR;
    g_printerr("error: couldn't create client for the IMS RTP service: %s\n",
               error->message);
    return;
  }
  runtime->imsrtp_ready = IMS_INIT_OK;
  g_printerr("IMS RTP Allocated!\n");
}

static void nas_allocate_client_ready(QmiDevice *dev, GAsyncResult *res) {
  GError *error = NULL;
  ctx->nas = qmi_device_allocate_client_finish(dev, res, &error);
  if (!ctx->nas) {
    runtime->nas_ready = IMS_INIT_ERR;

    g_printerr("error: couldn't create client for the NAS service: %s\n",
               error->message);
    exit(EXIT_FAILURE);
  }
  g_printerr("NAS Allocated!\n");
  runtime->nas_ready = IMS_INIT_OK;
  nas_start(dev, QMI_CLIENT_NAS(ctx->nas), ctx->cancellable);
}

static void imsa_allocate_client_ready(QmiDevice *dev, GAsyncResult *res) {
  GError *error = NULL;
  ctx->imsa = qmi_device_allocate_client_finish(dev, res, &error);
  if (!ctx->imsa) {
    runtime->imsa_ready = IMS_INIT_ERR;

    g_printerr("error: couldn't create client for the IMSA service: %s\n",
               error->message);
    exit(EXIT_FAILURE);
  }
  g_printerr("IMSA Allocated!\n");
  runtime->imsa_ready = IMS_INIT_OK;
  imsa_start(dev, QMI_CLIENT_IMSA(ctx->imsa), ctx->cancellable);
}

/* Allocate clients for all of our services
   We will finish when the callback is triggered from libqmi
*/
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
      dev, QMI_SERVICE_IMSA, QMI_CID_NONE, 10, ctx->cancellable,
      (GAsyncReadyCallback)imsa_allocate_client_ready, NULL);

  qmi_device_allocate_client(
      dev, QMI_SERVICE_IMS, QMI_CID_NONE, 10, ctx->cancellable,
      (GAsyncReadyCallback)imss_allocate_client_ready, NULL);

  qmi_device_allocate_client(
      dev, QMI_SERVICE_IMSP, QMI_CID_NONE, 10, ctx->cancellable,
      (GAsyncReadyCallback)imsp_allocate_client_ready, NULL);

  qmi_device_allocate_client(
      dev, QMI_SERVICE_IMSRTP, QMI_CID_NONE, 10, ctx->cancellable,
      (GAsyncReadyCallback)imsrtp_allocate_client_ready, NULL);
}


gboolean wait_for_init(void *data) {
  runtime->carrier_data = get_carrier_data();
  g_printerr("%s\n * Client allocation status:\n", __func__);
  g_printerr("\t - WDS: %i \n", runtime->wds_ready);
  g_printerr("\t - NAS: %i\n", runtime->nas_ready);
  g_printerr("\t - IMSS: %i\n", runtime->imss_ready);
  g_printerr("\t - IMSA: %i\n", runtime->imsa_ready);
  g_printerr("\t - IMSP: %i\n", runtime->imsp_ready);
  g_printerr("\t - IMS RTP: %i\n", runtime->imsrtp_ready);
  g_printerr(" * Network:\n");
  g_printerr("\t - MCC: %i\n", runtime->carrier_data.mcc);
  g_printerr("\t - MNC: %i\n", runtime->carrier_data.mnc);
  /*
  IMSRTP needs to be explicitly told to start with a subscription ID
  Me thought wrong. First I need to bring up the network, tell it
  the handler, and then I should be able to bring up rtp and presence.

  */
  guint8 wds_ready = wds_get_readiness_step();
  g_print("QMICLI: WDS Readiness state: %u\n", wds_ready);
  if (wds_ready > 12 && !is_sub_requested()) {
    g_print("Do\n");
    imsa_attempt_bind();
  }
  if (runtime->wds_ready == IMS_INIT_OK &&
      runtime->nas_ready == IMS_INIT_OK &&
      runtime->imss_ready == IMS_INIT_OK &&
      runtime->imsa_ready == IMS_INIT_OK &&
      runtime->imsp_ready == IMS_INIT_OK &&
      runtime->imsrtp_ready == IMS_INIT_OK) {
        // We can let go, everything is setup
        runtime->is_initialized = TRUE;
        return FALSE;
      }

  return TRUE;
  // false to break the timer
}
/* QMI over node or qrtr */
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

/* QRTR only */
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

/* Entry point here:
  We just connect either via device node or qrtr, then
  we delegate to libqmi the process of registering to each
  service.
*/
gboolean create_qmi_client_connection(GFile *file, GCancellable *cancellable) {
  g_autofree gchar *fd = NULL;
  ctx = g_slice_new(Context);
  runtime = g_slice_new(IMSD_runtime);
  ctx->cancellable = g_object_ref(cancellable);
  fd = g_file_get_path(file);
  g_timeout_add_seconds(10, wait_for_init, runtime);
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