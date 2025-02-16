/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Copyright (c) 2024, Biktorgj
 */
#include "qmi-ims-client.h"
#include "dcm.h"
#include "mfs.h"
#include "dms.h"
#include "imsa.h"
#include "imsd.h"
#include "imss.h"
#include "nas.h"
#include "wds.h"
#include "pdc.h"
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

IMSD_Runtime *imsd_runtime;
_IMSD_Client *imsd_client;
/* Helpers to (de) allocate services */

static gboolean close_device(gpointer userdata);
gboolean wait_for_init(void *data);

void cancel_connection_manager() {
  g_printerr("Stopping connmanager\n");
 // wds_do_stop_network(TRUE);
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
    if (imsd_client->handles.wds)
      g_printerr("WDS still allocated\n");
    if (imsd_client->handles.nas)
      g_printerr("NAS still allocated\n");
    if (imsd_client->handles.imss)
      g_printerr("IMSS still allocated\n");
    if (imsd_client->handles.imsa[0])
      g_printerr("IMSA still allocated\n");
    if (imsd_client->handles.imsp)
      g_printerr("IMSP still allocated\n");
    if (imsd_client->handles.imsrtp)
      g_printerr("IMSRTP still allocated\n");
    sleep(1);
  } while (imsd_client->handles.wds || imsd_client->handles.nas ||
           imsd_client->handles.imss || imsd_client->handles.imsa[0] ||
           imsd_client->handles.imsp || imsd_client->handles.imsrtp);

  g_printerr("Closing device\n");
  qmi_device_close_async(imsd_client->handles.device, 1, NULL,
                         (GAsyncReadyCallback)close_ready, NULL);

  return FALSE;
}

static void wds_release_client_ready(QmiDevice *dev, GAsyncResult *res) {
  GError *error = NULL;
  if (!qmi_device_release_client_finish(dev, res, &error)) {
    g_printerr("error: couldn't release WDS client: %s\n", error->message);
    g_error_free(error);
  } else {
    g_printerr("WDS Client released!\n");
    imsd_client->handles.wds = NULL;
  }
}

static void nas_release_client_ready(QmiDevice *dev, GAsyncResult *res) {
  GError *error = NULL;
  if (!qmi_device_release_client_finish(dev, res, &error)) {
    g_printerr("error: couldn't release NAS client: %s\n", error->message);
    g_error_free(error);
  } else {
    g_printerr("NAS Client released!\n");
    imsd_client->handles.nas = NULL;
  }
}

static void imss_release_client_ready(QmiDevice *dev, GAsyncResult *res) {
  GError *error = NULL;
  if (!qmi_device_release_client_finish(dev, res, &error)) {
    g_printerr("error: couldn't release IMSS client: %s\n", error->message);
    g_error_free(error);
  } else {
    g_printerr("IMSS Client released!\n");
    imsd_client->handles.imss = NULL;
  }
}

static void imsa_release_client_ready(QmiDevice *dev, GAsyncResult *res) {
  GError *error = NULL;
  if (!qmi_device_release_client_finish(dev, res, &error)) {
    g_printerr("error: couldn't release IMSA client: %s\n", error->message);
    g_error_free(error);
  } else {
    g_printerr("IMSA Client released!\n");
    imsd_client->handles.imsa[0] = NULL;
  }
}
/*
static void imsp_release_client_ready(QmiDevice *dev, GAsyncResult *res) {
  GError *error = NULL;
  if (!qmi_device_release_client_finish(dev, res, &error)) {
    g_printerr("error: couldn't release IMSP client: %s\n", error->message);
    g_error_free(error);
  } else {
    g_printerr("Client released!\n");
    imsd_client->handles.imsp = NULL;
  }
}
*/
static void imsrtp_release_client_ready(QmiDevice *dev, GAsyncResult *res) {
  GError *error = NULL;
  if (!qmi_device_release_client_finish(dev, res, &error)) {
    g_printerr("error: couldn't release IMS RTP client: %s\n", error->message);
    g_error_free(error);
  } else {
    g_printerr("Client released!\n");
    imsd_client->handles.imsrtp = NULL;
  }
}

void release_clients() {
  g_printerr("Releasing clients:\n");
  if (imsd_client->handles.wds) {
    g_printerr("* WDS\n");
    qmi_device_release_client(
        imsd_client->handles.device, imsd_client->handles.wds,
        QMI_DEVICE_RELEASE_CLIENT_FLAGS_NONE, 1, NULL,
        (GAsyncReadyCallback)wds_release_client_ready, NULL);
  }
  if (imsd_client->handles.nas) {
    g_printerr("* NAS\n");
    qmi_device_release_client(
        imsd_client->handles.device, imsd_client->handles.nas,
        QMI_DEVICE_RELEASE_CLIENT_FLAGS_NONE, 1, NULL,
        (GAsyncReadyCallback)nas_release_client_ready, NULL);
  }
  if (imsd_client->handles.imss) {
    g_printerr("* IMSS\n");
    qmi_device_release_client(
        imsd_client->handles.device, imsd_client->handles.imss,
        QMI_DEVICE_RELEASE_CLIENT_FLAGS_NONE, 1, NULL,
        (GAsyncReadyCallback)imss_release_client_ready, NULL);
  }
  if (imsd_client->handles.imsa[0]) {
    g_printerr("* IMSA\n");
    qmi_device_release_client(
        imsd_client->handles.device, imsd_client->handles.imsa[0],
        QMI_DEVICE_RELEASE_CLIENT_FLAGS_NONE, 1, NULL,
        (GAsyncReadyCallback)imsa_release_client_ready, NULL);
  }
  if (imsd_client->handles.imsp) {
    g_printerr("* IMSP\n");
 /*   qmi_device_release_client(
        imsd_client->handles.device, imsd_client->handles.imsp,
        QMI_DEVICE_RELEASE_CLIENT_FLAGS_NONE, 1, NULL,
        (GAsyncReadyCallback)imsp_release_client_ready, NULL);*/
  }
  if (imsd_client->handles.imsrtp) {
    g_printerr("* IMS RTP\n");
    qmi_device_release_client(
        imsd_client->handles.device, imsd_client->handles.imsrtp,
        QMI_DEVICE_RELEASE_CLIENT_FLAGS_NONE, 10, NULL,
        (GAsyncReadyCallback)imsrtp_release_client_ready, NULL);
  }
}

static void wds_allocate_client_ready(QmiDevice *dev, GAsyncResult *res, gpointer user_data) {
  GError *error = NULL;
  _WDS_Client *client = (_WDS_Client*) user_data;
  client->wds =
      qmi_device_allocate_client_finish(dev, res, &error);
  if (!client->wds) {
    client->wds_ready = IMS_INIT_ERR;

    g_printerr("error: couldn't create client for the WDSS service: %s\n",
               error->message);
    exit(EXIT_FAILURE);
  }
  g_printerr("WDS Allocated!\n");
  client->wds_ready = IMS_INIT_OK;
  wds_init_context(imsd_client->handles.device, imsd_runtime->cancellable);
}


static void imss_allocate_client_ready(QmiDevice *dev, GAsyncResult *res) {
  GError *error = NULL;
  imsd_client->handles.imss =
      qmi_device_allocate_client_finish(dev, res, &error);
  if (!imsd_client->handles.imss) {
    imsd_client->readiness.imss_ready = IMS_INIT_ERR;
    g_printerr("error: couldn't create client for the IMSS service: %s\n",
               error->message);
    exit(EXIT_FAILURE);
  }
  imsd_client->readiness.imss_ready = IMS_INIT_OK;
  g_printerr("IMSS Allocated!\n");
  imss_start(dev, QMI_CLIENT_IMS(imsd_client->handles.imss),
             imsd_runtime->cancellable);
}
/*
static void imsp_allocate_client_ready(QmiDevice *dev, GAsyncResult *res) {
  GError *error = NULL;
  imsd_client->handles.imsp =
      qmi_device_allocate_client_finish(dev, res, &error);
  if (!imsd_client->handles.imsp) {
    imsd_client->readiness.imsp_ready = IMS_INIT_ERR;

    g_printerr(
        "error: couldn't create client for the IMS Presenece service: %s\n",
        error->message);
    return;
  }
  imsd_client->readiness.imsp_ready = IMS_INIT_OK;
  g_printerr("IMS Presence Allocated!\n");
}*/

static void imsrtp_allocate_client_ready(QmiDevice *dev, GAsyncResult *res) {
  GError *error = NULL;
  imsd_client->handles.imsrtp =
      qmi_device_allocate_client_finish(dev, res, &error);
  if (!imsd_client->handles.imsrtp) {
    imsd_client->readiness.imsrtp_ready = IMS_INIT_ERR;
    g_printerr("error: couldn't create client for the IMS RTP service: %s\n",
               error->message);
    return;
  }
  imsd_client->readiness.imsrtp_ready = IMS_INIT_OK;
  g_printerr("IMS RTP Allocated!\n");
}

static void nas_allocate_client_ready(QmiDevice *dev, GAsyncResult *res) {
  GError *error = NULL;
  imsd_client->handles.nas =
      qmi_device_allocate_client_finish(dev, res, &error);
  if (!imsd_client->handles.nas) {
    imsd_client->readiness.nas_ready = IMS_INIT_ERR;

    g_printerr("error: couldn't create client for the NAS service: %s\n",
               error->message);
    exit(EXIT_FAILURE);
  }
  g_printerr("NAS Allocated!\n");
  imsd_client->readiness.nas_ready = IMS_INIT_OK;
  nas_start(dev, QMI_CLIENT_NAS(imsd_client->handles.nas),
            imsd_runtime->cancellable);
}

static void pdc_allocate_client_ready(QmiDevice *dev, GAsyncResult *res) {
  GError *error = NULL;
  imsd_client->handles.pdc =
      qmi_device_allocate_client_finish(dev, res, &error);
  if (!imsd_client->handles.pdc) {
    imsd_client->readiness.pdc_ready = IMS_INIT_ERR;

    g_printerr("error: couldn't create client for the PDC service: %s\n",
               error->message);
    exit(EXIT_FAILURE);
  }
  g_printerr("PDC Allocated!\n");
  imsd_client->readiness.pdc_ready = IMS_INIT_OK;
  pdc_start(dev, QMI_CLIENT_PDC(imsd_client->handles.pdc),
            imsd_runtime->cancellable);
}

static void imsa_allocate_client_ready(QmiDevice *dev, GAsyncResult *res) {
  GError *error = NULL;
  imsd_client->handles.imsa[0] =
      qmi_device_allocate_client_finish(dev, res, &error);
  if (!imsd_client->handles.imsa[0]) {
    imsd_client->readiness.imsa_ready = IMS_INIT_ERR;

    g_printerr("error: couldn't create client for the IMSA service: %s\n",
               error->message);
    exit(EXIT_FAILURE);
  }
  g_printerr("IMSA Allocated!\n");
  imsd_client->readiness.imsa_ready = IMS_INIT_OK;
  imsa_start(dev, QMI_CLIENT_IMSA(imsd_client->handles.imsa[0]),
             imsd_runtime->cancellable);
}

static void dms_allocate_client_ready(QmiDevice *dev, GAsyncResult *res) {
  GError *error = NULL;
  imsd_client->handles.dms =
      qmi_device_allocate_client_finish(dev, res, &error);
  if (!imsd_client->handles.dms) {
    imsd_client->readiness.dms_ready = IMS_INIT_ERR;

    g_printerr("error: couldn't create client for the DMS service: %s\n",
               error->message);
    exit(EXIT_FAILURE);
  }
  g_printerr("DMS Allocated!\n");
  imsd_client->readiness.dms_ready = IMS_INIT_OK;
  dms_start(dev, QMI_CLIENT_DMS(imsd_client->handles.dms),
            imsd_runtime->cancellable);
}

static void mfs_allocate_client_ready(QmiDevice *dev, GAsyncResult *res) {
  GError *error = NULL;
  imsd_client->handles.mfs =
      qmi_device_allocate_client_finish(dev, res, &error);
  if (!imsd_client->handles.mfs) {
    imsd_client->readiness.mfs_ready = IMS_INIT_ERR;

    g_printerr("error: couldn't create client for the MFS service: %s\n",
               error->message);
    exit(EXIT_FAILURE);
  }
  g_printerr("MFS Allocated!\n");
  imsd_client->readiness.mfs_ready = IMS_INIT_OK;
  mfs_allocate(dev, QMI_CLIENT_MFS(imsd_client->handles.mfs),
            imsd_runtime->cancellable);
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

  /* We will use a client for each slot in WDS */
  for (uint8_t i = 0; i < MAX_SIM_SLOTS; i++) {
      qmi_device_allocate_client(
      dev, QMI_SERVICE_WDS, QMI_CID_NONE, 10, imsd_runtime->cancellable,
      (GAsyncReadyCallback)wds_allocate_client_ready, &imsd_client->WDS_Client[i]);
  }

  qmi_device_allocate_client(
      dev, QMI_SERVICE_NAS, QMI_CID_NONE, 10, imsd_runtime->cancellable,
      (GAsyncReadyCallback)nas_allocate_client_ready, NULL);

  qmi_device_allocate_client(
      dev, QMI_SERVICE_PDC, QMI_CID_NONE, 10, imsd_runtime->cancellable,
      (GAsyncReadyCallback)pdc_allocate_client_ready, NULL);

  qmi_device_allocate_client(
      dev, QMI_SERVICE_IMSA, QMI_CID_NONE, 10, imsd_runtime->cancellable,
      (GAsyncReadyCallback)imsa_allocate_client_ready, NULL);

  qmi_device_allocate_client(
      dev, QMI_SERVICE_DMS, QMI_CID_NONE, 10, imsd_runtime->cancellable,
      (GAsyncReadyCallback)dms_allocate_client_ready, NULL);

  qmi_device_allocate_client(
      dev, QMI_SERVICE_IMS, QMI_CID_NONE, 10, imsd_runtime->cancellable,
      (GAsyncReadyCallback)imss_allocate_client_ready, NULL);

  qmi_device_allocate_client(
      dev, QMI_SERVICE_IMSRTP, QMI_CID_NONE, 10, imsd_runtime->cancellable,
      (GAsyncReadyCallback)imsrtp_allocate_client_ready, NULL);
 
  qmi_device_allocate_client(
      dev, QMI_SERVICE_MFS, QMI_CID_NONE, 10, imsd_runtime->cancellable,
      (GAsyncReadyCallback)mfs_allocate_client_ready, NULL);
}
/*
static void do_allocate_rest(QmiDevice *dev) {
  qmi_device_allocate_client(
      dev, QMI_SERVICE_IMSP, QMI_CID_NONE, 10, imsd_runtime->cancellable,
      (GAsyncReadyCallback)imsp_allocate_client_ready, NULL);
}
*/
gboolean wait_for_init(void *data) {
  imsd_runtime->current_network_provider[0] = get_carrier_data();
  g_print("[QMI Client] Client allocation status:\n");
  g_print(" - Network Access Service: %s\n", imsd_client->readiness.nas_ready ? "Ready":"Not ready" );
  g_print(" - Persistent Device Configuration Service: %s\n", imsd_client->readiness.pdc_ready ? "Ready":"Not ready" );
  g_print(" - IMS Settings Service: %s\n", imsd_client->readiness.imss_ready? "Ready":"Not ready");
  g_print(" - IMS Application Service: %s\n", imsd_client->readiness.imsa_ready? "Ready":"Not ready");
  g_print(" - Device Management Service: %s\n", imsd_client->readiness.dms_ready? "Ready":"Not ready");
  g_print(" - IMS Presence Service: %s\n", imsd_client->readiness.imsp_ready? "Ready":"Not ready");
  g_print(" - IMS Real Time Transport Protocol Service: %s\n", imsd_client->readiness.imsrtp_ready? "Ready":"Not ready");
  g_print(" - Modem Filesystem Service: %s\n", imsd_client->readiness.mfs_ready? "Ready":"Not ready");
  for (uint8_t i = 0; i < MAX_SIM_SLOTS; i++) {
    g_print(" - SIM Slot %u\n", i);
    g_print("   - Network: %i-%i\n", imsd_runtime->current_network_provider[i].mcc, imsd_runtime->current_network_provider[i].mnc);
    g_print("   - Voice Service (call monitoring) (Slot %u): %s\n",i,  imsd_client->readiness.voice_svc_ready[i]? "Ready":"Not ready");
    g_print("   - Wireless Data Service (Slot %u): %s \n",i ,imsd_client->WDS_Client[i].wds_ready? "Ready":"Not ready");
  }
  /*
  IMSRTP needs to be explicitly told to start with a subscription ID
  Me thought wrong. First I need to bring up the network, tell it
  the handler, and then I should be able to bring up rtp and presence.
  I probably need to use the subscription and instance ID the DCM
  gives me.
  
  Another thing to improve is error handling in the DCM. If you
  tell it you started the network, returning an OK to the 
  activate request, but you didn't actually start it or if it
  failed for some reason, it starts spamming the server until 
  everything falls apart (and by everything I mean modemmanager 
  ends up stopping the modem and restarting it or you end up
  with a crashdump)
  */

/*  if (wds_get_readiness_step() == WDS_CONNECTION_STATE_FINISHED) {
    uint8_t ipaddr[128] = {0};
    wds_copy_ip_address(ipaddr);
    notify_pdp_ipaddress_change(ipaddr);
  }*/
  get_ims_services_state();
  get_registration_state();
  //  imss_get_ims_ua();
  if (/*imsd_client->readiness.wds_ready == IMS_INIT_OK &&*/
      imsd_client->readiness.dms_ready == IMS_INIT_OK &&
      imsd_client->readiness.nas_ready == IMS_INIT_OK &&
      imsd_client->readiness.imss_ready == IMS_INIT_OK &&
      imsd_client->readiness.imsa_ready == IMS_INIT_OK/* &&
      imsd_client->readiness.imsp_ready == IMS_INIT_OK &&
      imsd_client->readiness.imsrtp_ready == IMS_INIT_OK*/) {
    // We can let go, everything is setup
    imsd_client->readiness.is_initialized = TRUE;
    return TRUE;
  }

  return TRUE;
  // false to break the timer
}

/* Pass trhu to the WDS service*/
void request_network_start(uint32_t sim_slot) {
  g_print("[QMI Client] Requesting Wireless Data Service activation for Sim slot %u\n", sim_slot);
  /* We use the sim slot to track which struct to use */
 initiate_wds_session(&imsd_client->WDS_Client[sim_slot], sim_slot);
}




/* QMI over node or qrtr */
static void qmi_device_ready(GObject *unused, GAsyncResult *res) {
  QmiDeviceOpenFlags open_flags = QMI_DEVICE_OPEN_FLAGS_NONE;
  GError *error = NULL;

  imsd_client->handles.device = qmi_device_new_finish(res, &error);
  if (!imsd_client->handles.device) {
    g_printerr("error: couldn't create QmiDevice: %s\n", error->message);
    exit(EXIT_FAILURE);
  }
  /* Setup device open flags */
  open_flags |= QMI_DEVICE_OPEN_FLAGS_EXPECT_INDICATIONS;

  /* Open the device */
  qmi_device_open(imsd_client->handles.device, open_flags, 15,
                  imsd_runtime->cancellable,
                  (GAsyncReadyCallback)device_open_ready, NULL);
}

/* QRTR only */
static void bus_new_callback(GObject *source, GAsyncResult *res,
                             gpointer user_data) {
  g_autoptr(GError) error = NULL;
  guint node_id;
  QrtrNode *node;

  node_id = GPOINTER_TO_UINT(user_data);

  imsd_client->handles.qrtr_bus = qrtr_bus_new_finish(res, &error);
  if (!imsd_client->handles.qrtr_bus) {
    g_printerr("error: couldn't access QRTR bus: %s\n", error->message);
    exit(EXIT_FAILURE);
  }

  node = qrtr_bus_peek_node(imsd_client->handles.qrtr_bus, node_id);
  if (!node) {
    g_printerr("error: node with id %u not found in QRTR bus\n", node_id);
    exit(EXIT_FAILURE);
  }

  qmi_device_new_from_node(node, imsd_runtime->cancellable,
                           (GAsyncReadyCallback)qmi_device_ready, NULL);
}

/* Entry point here:
  We just connect either via device node or qrtr, then
  we delegate to libqmi the process of registering to each
  service.
*/
gpointer initialize_qmi_client(gpointer user_data) {
  imsd_runtime = (IMSD_Runtime *)user_data;
  imsd_client = g_new(_IMSD_Client, 1);
  g_autofree gchar *fd = NULL;
     qmi_utils_set_traces_enabled (TRUE);
    qmi_utils_set_show_personal_info (TRUE);
  g_print("[QMI Client] Start\n");

  fd = g_file_get_path(imsd_runtime->client_path);
  g_timeout_add_seconds(10, wait_for_init, imsd_runtime);
  if (fd) {
    g_info("Connecting via device node");
    qmi_device_new(imsd_runtime->client_path, imsd_runtime->cancellable,
                   (GAsyncReadyCallback)qmi_device_ready, NULL);
    return NULL;
  } else {
    g_info("Connecting via QRTR");
    guint32 node_id;
    fd = g_file_get_uri(imsd_runtime->client_path);
    if (qrtr_get_node_for_uri(fd, &node_id)) {
      qrtr_bus_new(1000, imsd_runtime->cancellable,
                   (GAsyncReadyCallback)bus_new_callback,
                   GUINT_TO_POINTER(node_id));
      return NULL;
    }
    g_printerr("Failed to connect via QRTR");
    return NULL;
  }
  g_printerr("Failed to determine the device type");
  return NULL;
}