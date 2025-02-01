/* SPDX-License-Identifier: GPL-3 */

#ifndef __IMSD_H__
#define __IMSD_H__

#include <gio/gio.h>
#include <glib-unix.h>
#include <glib.h>
#include <libqmi-glib.h>
#include <libqrtr-glib.h>
#include <stdint.h>
/* Here goes all the internal IMS Daemon stuff */
#define PROG_NAME "imsd"
#define RELEASE_VER "0.0.5"
#define LOCK_FILE "/tmp/imsd.lock"
#define CONFIG_FILE "/etc/imsd/imsd.conf"
#define PROFILE_PATH "/etc/imsd/carriers"

/* Nobody worry, this is just temporary*/
#define PDC_FOLDER_PATH "/root/mcfgs"
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
  uint8_t slot;
  uint16_t mcc;
  uint16_t mnc;
  uint32_t signal_level;
  uint8_t rat_type;
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
  QmiClient *imsp;
  QmiClient *imsrtp;
  QmiClient *dms;
  QmiClient *pdc;
  QmiClient *imsa[MAX_SIM_SLOTS]; // One-per-sim!
  QmiClient *wds_ipv4[MAX_SIM_SLOTS];
  QmiClient *wds_ipv6[MAX_SIM_SLOTS];
} _QMI_Client_Handle;

typedef struct {
  uint8_t profile_id;
  gulong network_started_id;
  uint8_t packet_status_timeout_id;
  // Handler to start and stop network
  guint32 packet_data_handle;
  // WDS network bringup step
  uint8_t connection_readiness_step;
  // Mux ID and device in netlink
  // We should clean this up when we quit or we crash
  // **without** interfering with ModemManager...
  // Current IP Address
  uint8_t ip_addr_type;
  uint8_t ip_address[128];

  gchar *link_name;
  guint mux_id;
  uint8_t setup_link_done;
  // Endpoint
  uint8_t endpoint_type;
  uint8_t endpoint_ifnum;
} _WDS_Packet_Session;

typedef struct {
  guint i;
  GArray *profile_list;
} _Profile_List;

typedef struct {
  QmiClient *wds;
  uint8_t wds_ready;
  uint32_t slot_id;
  _WDS_Packet_Session packet_session;
  _Profile_List *profile_list;
} _WDS_Client;

typedef struct {
  uint8_t status;
  uint8_t is_initialized;
  uint8_t exit_requested;
  uint8_t apn_status;
  gchar *curr_apn;
  uint8_t nas_ready;
  uint8_t imss_ready;
  uint8_t imsa_ready;
  uint8_t imsp_ready;
  uint8_t imsrtp_ready;
  uint8_t dms_ready;
  uint8_t wds_ready[MAX_SIM_SLOTS];
  // More to come
  uint8_t pdc_ready;
  uint8_t mfs_ready;
  uint8_t voice_svc_ready[MAX_SIM_SLOTS];

} _IMSD_Client_Readiness;

typedef struct {
  _IMSD_Client_Readiness readiness;
  _QMI_Client_Handle handles;
  _WDS_Client WDS_Client[MAX_SIM_SLOTS];
} _IMSD_Client;


/* We probably have two sim slots
   so we need to be able to track
   two concurrent ims sessions */
typedef struct {
  uint8_t is_enabled;
  uint32_t packet_handle; // WDS Packet handle associated with this PDP Session
  uint8_t internal_pdp_id;
  uint32_t pdp_sequence_id;
  uint32_t pdp_instance_id;
  uint32_t slot_id;
  uint32_t pdp_subscription_id;
} _DCM_PDP_Session;


typedef struct {
  GFile *client_path;
  GCancellable *cancellable;
  uint8_t terminate;
  GMutex mutex;
  GCond cond;
  uint8_t ims_client_ready;
  uint8_t ims_server_ready;
  uint8_t sim_slot_active[MAX_SIM_SLOTS];
  _Network_Provider_Data current_network_provider[MAX_SIM_SLOTS];
  //  _QMI_Client *qmi_client;
  //  _QRTR_Server *qrtr_server;
} IMSD_Runtime;

#endif