/* SPDX-License-Identifier: GPL-3 */

#ifndef __IMSD_H__
#define __IMSD_H__

#include <gio/gio.h>
#include <glib-unix.h>
#include <glib.h>
#include <libqmi-glib.h>
#include <libqrtr-glib.h>

/* Here goes all the internal IMS Daemon stuff */
#define PROG_NAME "imsd"
#define RELEASE_VER "0.0.3"
#define LOCK_FILE "/tmp/imsd.lock"
#define CONFIG_FILE "/etc/imsd.conf"
#define PROFILE_PATH "/usr/share/imsd/profiles/"

/* Limits */
#define MAX_SIM_SLOTS 2

enum {
  IMS_REPLY_OK = 0x00,
  IMS_NOT_READY = 0x01,
  IMS_NOT_AVAILABLE = 0x02,
  IMS_READ_FAILED = 0x03,
  IMS_WRITE_FAILED = 0x04,
  IMS_INTERNAL_ERR = 0x05,
  IMS_ERR_MAX = 0xff
};

typedef struct {
  guint16 mcc;
  guint16 mnc;
  guint32 signal_level;
  guint8 rat_type;
} _Network_Provider_Data;

typedef struct {
  int sock_fd;
} _QRTR_Server;

typedef struct {
  QmiDevice *device;
  QrtrBus *qrtr_bus;
  QmiClient *wds;
  QmiClient *nas;
  QmiClient *imss;
  QmiClient *imsa;
  QmiClient *imsp;
  QmiClient *imsrtp;
  QmiClient *dms;
} _QMI_Client;

typedef struct {
  gboolean status;
  guint apn_status;
  gchar *curr_apn;
  guint wds_ready;
  guint nas_ready;
  guint imss_ready;
  guint imsa_ready;
  guint imsp_ready;
  guint imsrtp_ready;
  guint dms_ready;
  gboolean is_initialized;
  gboolean exit_requested;
} _IMSD_Runtime_State;

typedef struct {
  guint32 packet_data_handle;
} _WDS_Connection;

typedef struct {
  GFile *client_path;
  GCancellable *cancellable;
  gboolean terminate;
  GMutex mutex;
  GCond cond;
  guint8 ims_client_ready;
  guint8 ims_server_ready;
  guint8 sim_slot_active[MAX_SIM_SLOTS];
  _Network_Provider_Data *current_network_provider[MAX_SIM_SLOTS];
  //  _QMI_Client *qmi_client;
  //  _QRTR_Server *qrtr_server;
  
  _WDS_Connection *wds_connection;
  _IMSD_Runtime_State *runtime_state;
} IMSD_Runtime;

#endif