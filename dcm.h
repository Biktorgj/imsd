/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Copyright (c) 2024, Biktorgj
 */

#ifndef DCM_H
#define DCM_H

#include <gio/gio.h>
#include <glib-unix.h>
#include <glib.h>
#include "qmi-util.h"

G_BEGIN_DECLS

/* Our very first QRTR service! */
#define SERVICE_ID 0x0302  // Replace with your service ID
#define VERSION_ID 1 // No clue if this is right
#define INSTANCE_ID 1    // Since there's nobody else around we can take this

/*
So it turns out we need to emulate a QMI server for DCM support
The Modem DSP expects to us to be available to to be queried about
the IMS link, and we must answer it with what it wants.

Here we make some good ol' packed structs to answer its queries
*/

/* Available commands */
enum {
  IMS_DCM_PDP_ACTIVATE = 0x0020,
  IMS_DCM_LINK_ADD = 0x0023,
  IMS_DCM_REGISTER = 0x002e,
  IMS_DCM_ENABLE_STATUS = 0x0034,
  IMS_DCM_CLEAR = 0x0033,
};

/*
00 <-- REQUEST || RESPONSE || INDICATION (0x00 || 0x01 || 0x02)
01 00 <-- Transaction ID
23 00 <-- Message ID
23 00 <-- Message size

01 [id]
20 00 [len]
8b 23 [ port ]
01 00 00 00 [family -ipv6: 0x01 || ipv4 0x00]
19 66 65 38 30 3a 3a 39 35 31 65 3a 33 37 66 30 3a 39 33 61 61 3a 65 38 62 39
sz [ aaaaddrreeeesssss ]
of
addr

*/

/* TLVs */

/* Link Add Request */
struct ims_dcm_link_add_request {
  uint8_t id;   // 0x01
  uint16_t len; // 0x20
  uint16_t port;
  uint32_t ip_family;
  uint8_t ipaddr_sz;
  uint8_t *ipaddr; // Some IPv6 address?
} __attribute__((packed));

/* Link Add Response */
// struct qmi_generic_result_ind ims_dcm_link_add_resp;

/* PDP Activate Request */
/* --> Example pkt
00 
04 00 
20 00 
33 00 

--> apn data
01 
14 00 
03 69 6d 73 00 00 00 00 02 00 00 00 00 00 00 00 02 00 ff ff 
^   i  m  s [apn type  ][ RAT type] [ip family]
3 char apn size!

10 
04 00 
65 00 00 00 
[ pdp sequence ]
11 
04 00 
00 00 00 00 
[ pdp subscription id ]
12 
04 00 
01 00 00 00 
[sim slot ]
13 
04 00 
01 00 00 00 
[ pdp instance? epc instance? ]
*/
struct apn_data {
  uint8_t id;   // 0x01 [ qmi standard response]
  uint16_t len; // 0x04 for QMI std response
  uint8_t apn_name_len; //first char size of string
  uint8_t *apn_name;
  uint32_t apn_type; // 0 LTE || 1 Internet || 2 Emergency ...??
  int32_t rat_type; // 0 CDMA? || 1 LTE || 2 EPC
  int32_t ip_family; // 0 IPv4 || 1 IPv6
  uint16_t profile_id_3gpp; // 81voltd says this is the 3gpp index
  uint16_t profile_id_3gpp2; // In my case it's always 02 00, ff ff
} __attribute__((packed));

/* PDP Activate Request */
struct ims_dcm_pdp_activate_req {
  // struct apn_data apn_data; // 0x01
  struct qmi_generic_uint32_t_tlv pdp_sequence; // 0x10
  struct qmi_generic_uint32_t_tlv pdp_subscription_id; // 0x11
  struct qmi_generic_uint32_t_tlv requesting_sim_slot; // 0x12
  struct qmi_generic_uint32_t_tlv pdp_instance; // 0x13 Unsure about this one
} __attribute__((packed));
/* Unsure how I'm supposed to know the size of apn name */

struct ims_dcm_pdp_activate_response {
  struct qmi_generic_result_ind response;
  struct qmi_generic_uint8_t_tlv pdp_id; // 0x10
  struct qmi_generic_uint32_t_tlv pdp_sequence; // 0x11
  struct qmi_generic_uint32_t_tlv pdp_instance; // 0x12 Unsure about this one

} __attribute__((packed));

/* PDP Activate indication: address change*/
struct ipaddr_block{
  uint8_t id;
  uint16_t len;
  uint32_t ip_family; // 0x00 == IPv4 || 0x01 == IPv6
  uint8_t ip_addr_sz; // Size of ip address
  uint8_t *ipaddr[0];
} __attribute__((packed));

struct ims_dcm_pdp_activate_indication {
  struct qmi_generic_result_ind response; // 0x02
  struct qmi_generic_uint8_t_tlv pdp_id; // 0x01
  struct qmi_generic_uint32_t_tlv pdp_sequence; // 0x10
  struct ipaddr_block ipaddr; // 0x11 New IP address
  struct qmi_generic_uint32_t_tlv pdp_instance; // 0x12 Unsure about this one
} __attribute__((packed));

/* Register request and response */
/*
00 < request
02 00 < transaction id
2e 00 < message id
0b 00 < msg size
10 < tlv
01 00 < 1 byte len
01 <-- *something* needs to be set
11 < tlv
04 00 <4 byte len
00 00 00 00 

This one is funny. If you don't reply to the modem
pdp activate req with what it wants it seems to try this. 
When this fails (or always?) it then asks for 0x0034

At least for now, we eat it and reply with a standard
QMI response to make it happy
*/

struct ims_dcm_register_request {
 uint8_t id;   // 0x01 [ qmi standard response]
 uint16_t len; // 0x0b 
 uint8_t *data;
} __attribute__((packed));

// struct qmi_generic_result_ind ims_dcm_register_response;

/*
00 
03 00 
34 00 
0b 00 

01 
08 00 <- u64
00 00 00 00 00 00 00 00 
Easy enough
*/
struct ims_dcm_enable_status_req {
 uint8_t id;   // 0x01 [ qmi standard response]
 uint16_t len; // 0x08
 uint64_t ims_dcm_status;
} __attribute__((packed));

// struct qmi_generic_result_ind ims_dcm_enable_status_response;

// void dcm_start(GCancellable *cancellable);
gpointer initialize_qmi_server(gpointer user_data);
gboolean notify_pdp_ipaddress_change(uint8_t *ipaddr);
G_END_DECLS

#endif
