/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "dcm.h"
#include "imsd.h"
#include "qmi-util.h"
#include "qmi-ims-client.h"
#include <gio/gio.h>
#include <glib-unix.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <libqmi-glib.h>
#include <libqrtr-glib.h>
#include <libqrtr.h>
#include <linux/qrtr.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define DEBUG_DCM

/*
 IMS DCM Service implementation

*/
/* Most of the QRTR code comes from the examples at
  https://github.com/linux-msm/qrtr/blob/master/lib/qrtr.c

*/

typedef struct {
  int sock_fd;
  GMainLoop *server_loop;
  IMSD_Runtime *imsd_runtime;
  GHashTable *clients;
  // We need to repurpose these:
  uint16_t transaction_id;
  _DCM_PDP_Session PDP_Session[MAX_SIM_SLOTS];
} QRTRServer;

QRTRServer *server;

/*
 * FIXME: MultiSIM support.
 * The DCM code is able to handle multiple SIM slots but
 * WDS and NAS need to work with it, so I just force it 
 * to slot 1 for now
 */
gboolean notify_pdp_ipaddress_change(uint32_t slot_id, uint8_t *ip_address) { 
  char *outbuff = calloc(1024, sizeof(uint8_t));
  GHashTableIter iter;
  struct sockaddr_qrtr *client_addr;
  gpointer client_state;
  ssize_t outbuff_len = 0;
  int active_sim_slot = -1;

  for (int i = 0; i < MAX_SIM_SLOTS; i++) {
    if (slot_id == server->PDP_Session[i].slot_id) {
      active_sim_slot = i;
    }
  }
  if (active_sim_slot == -1) {
    g_print("[DCM] Fatal: Can't find the SIM where the connection is\n");
    free(outbuff);
    return FALSE;
  }

  if (server->transaction_id == 99) {
    server->transaction_id = 1;
  } else {
    server->transaction_id++;
  }
  g_print("[DCM] Notify PDP IP Address Change: %s\n", ip_address);
  /* Get some stuff ready */
  /* Set the transaction ID */
  struct qmi_packet *pkt = (struct qmi_packet *)outbuff;
  pkt->message_type = 0x04; // Indication!
  pkt->msgid = IMS_DCM_PDP_ACTIVATE;
  pkt->transaction_id = server->transaction_id;
  outbuff_len += sizeof(struct qmi_packet);
 
  /* QMI Response comes first */
  struct qmi_generic_result_ind *response =
      (struct qmi_generic_result_ind *)(outbuff + outbuff_len);
  response->result_code_type = 0x02;
  response->generic_result_size = 0x04;
  response->result = 0x00;
  response->response = 0x00;
  outbuff_len += sizeof(struct qmi_generic_result_ind);

  /* Our Local ID */
  struct qmi_generic_uint8_t_tlv *pdp_id =
      (struct qmi_generic_uint8_t_tlv *)(outbuff + outbuff_len);
  pdp_id->id = 0x01;
  pdp_id->len = 0x01;
  pdp_id->data = server->PDP_Session[active_sim_slot].internal_pdp_id;
  outbuff_len += sizeof(struct qmi_generic_uint8_t_tlv);
  
  /* Sequence, as provided by the activation request */
  struct qmi_generic_uint32_t_tlv *pdp_sequence =
      (struct qmi_generic_uint32_t_tlv *)(outbuff + outbuff_len);
  pdp_sequence->id = 0x10;
  pdp_sequence->len = 0x04;
  pdp_sequence->data = server->PDP_Session[active_sim_slot].pdp_sequence_id;
  outbuff_len += sizeof(struct qmi_generic_uint32_t_tlv);

  /* IP Address we got from the WDS service when we started the network */
  struct ipaddr_block *ipaddr = (struct ipaddr_block *)(outbuff + outbuff_len);
  ipaddr->id = 0x11;
  ipaddr->ip_addr_sz = strlen((char *)ip_address);
  ipaddr->ip_family = 0x00; // IPv4 for testing
  memcpy(&ipaddr->ipaddr, ip_address,  ipaddr->ip_addr_sz);
  ipaddr->len = sizeof(struct ipaddr_block) + ipaddr->ip_addr_sz - 3;
  outbuff_len +=  sizeof(struct ipaddr_block) + ipaddr->ip_addr_sz;

  /* The PDP Instance we got from the activation request */
  struct qmi_generic_uint32_t_tlv *pdp_instance =
      (struct qmi_generic_uint32_t_tlv *)(outbuff + outbuff_len);
  pdp_instance->id = 0x12;
  pdp_instance->len = 0x04;
  pdp_instance->data = server->PDP_Session[active_sim_slot].pdp_instance_id;
  outbuff_len += sizeof(struct qmi_generic_uint32_t_tlv);

  pkt->length = outbuff_len - sizeof(struct qmi_packet);

#ifdef DEBUG_DCM
  g_print(" ------- \n[DCM] Indication message dump:\n");
  for (int i = 0; i < outbuff_len; i++) {
    g_print("%.2x ", outbuff[i]);
  }
  g_print("\n ------- \n");
#endif

  g_hash_table_iter_init(&iter, server->clients);
  while (g_hash_table_iter_next(&iter, (gpointer *)&client_addr, &client_state)) {
        socklen_t addr_len = sizeof(*client_addr);
        ssize_t bytes_sent =  sendto(server->sock_fd, outbuff, outbuff_len, 0,
         (struct sockaddr *)client_addr, addr_len);
        if (bytes_sent < 0) {
            g_print("[DCM] Failed to send to the client at node %u with port %u\n", client_addr->sq_node, client_addr->sq_port);
        } else {
            g_print("Sent to client at node: %u / port: %u)\n",
                    client_addr->sq_node, client_addr->sq_port);
        }
    }
 
  if (outbuff)
    free(outbuff);
  return TRUE;
}

static gboolean handle_incoming_qrtr_request(GIOChannel *source,
                                             GIOCondition condition,
                                             gpointer user_data) {
  QRTRServer *server = (QRTRServer *)user_data;

  struct sockaddr_qrtr client_addr;
  socklen_t addr_len = sizeof(client_addr);
  char inbuff[1024];
  char *outbuff = calloc(1024, sizeof(uint8_t));
  uint32_t sim_slot = 0;
  uint16_t offset;
  ssize_t inbuff_len = -1;
  ssize_t outbuff_len = 0;

  if (condition & G_IO_IN) {
    inbuff_len =
        recvfrom(server->sock_fd, inbuff, sizeof(inbuff) - 1, 0,
                 (struct sockaddr *)&client_addr, &addr_len);
    if (inbuff_len < 0) {
      g_print("[DCM] Invalid packet!");
      free(outbuff);
      return TRUE;
    }

    gpointer client_key = g_memdup2(&client_addr, sizeof(client_addr));
    if (!g_hash_table_contains(server->clients, client_key)) {
        g_hash_table_insert(server->clients, client_key, g_strdup("Client state"));
        g_print("New client added (node: %u, port: %u).\n", client_addr.sq_node, client_addr.sq_port);
    } else {
        g_print("Client already existed: (node: %u, port: %u).\n", client_addr.sq_node, client_addr.sq_port);
        g_free(client_key); // Client already exists; free the duplicate
    }

#ifdef DEBUG_DCM
    /* I want to see the actual data tyvm */
    g_print(" ------- \n[DCM] Received message:\n");
    for (int i = 0; i < inbuff_len; i++) {
      g_print("%.2x ", inbuff[i]);
    }
    g_print("\n ------- \n");
#endif
    /* Get some stuff ready */
    struct qmi_packet *pkt = (struct qmi_packet *)outbuff;
    pkt->message_type = 0x02; // response!
    pkt->msgid = get_qmi_message_id(inbuff, inbuff_len);
    /* Set the transaction ID */
    pkt->transaction_id = get_qmi_transaction_id(inbuff, inbuff_len);
    outbuff_len += sizeof(struct qmi_packet);
    struct qmi_generic_result_ind *response; // Will repurpose this later

    switch (get_qmi_message_id(inbuff, inbuff_len)) {
    case IMS_DCM_PDP_ACTIVATE:
        /* Get the SIM Slot from the request */
      offset = get_tlv_offset_by_id((void *)inbuff, inbuff_len, 0x12);
      if (offset > 0) {
        struct qmi_generic_uint32_t_tlv *incoming_sim_slot =
            (struct qmi_generic_uint32_t_tlv *)(inbuff + offset);
        sim_slot = incoming_sim_slot->data;
        g_print(" - SIM Slot: %u\n", sim_slot);
      } else {
        g_printerr("[DCM] SIM Slot not available, assuming the first\n");
      }

      g_print("[DCM] PDP Activate Request!\n");
      struct ims_dcm_pdp_activate_response *pdp_res =
          (struct ims_dcm_pdp_activate_response *)(outbuff + outbuff_len);

      /* First we get what we need */
      /* Get the PDP Connection ID from the request */
      offset = get_tlv_offset_by_id((void *)inbuff, inbuff_len, 0x10);
      if (offset > 0) {
        struct qmi_generic_uint8_t_tlv *incoming_pdp_id =
            (struct qmi_generic_uint8_t_tlv *)(inbuff + offset);
        server->PDP_Session[sim_slot].pdp_sequence_id = incoming_pdp_id->data;
        g_print(" - PDP Sequence ID: %.4x\n", server->PDP_Session[sim_slot].pdp_sequence_id);
      }

      /* Get the sequence ID from the request */
      offset = get_tlv_offset_by_id((void *)inbuff, inbuff_len, 0x11);
      if (offset > 0) {
        struct qmi_generic_uint32_t_tlv *incoming_subscription_id =
            (struct qmi_generic_uint32_t_tlv *)(inbuff + offset);
        server->PDP_Session[sim_slot].pdp_subscription_id = incoming_subscription_id->data;
        g_print(" - PDP Subscription ID: %.4x\n", server->PDP_Session[sim_slot].pdp_subscription_id);
      }

      /* Get the SIM Slot from the request */
      offset = get_tlv_offset_by_id((void *)inbuff, inbuff_len, 0x12);
      if (offset > 0) {
        struct qmi_generic_uint32_t_tlv *incoming_sim_slot =
            (struct qmi_generic_uint32_t_tlv *)(inbuff + offset);
        server->PDP_Session[sim_slot].slot_id = incoming_sim_slot->data;
        g_print(" - SIM Slot: %u\n", server->PDP_Session[sim_slot].slot_id);
      }

      /* Get our unique instance ID */
      offset = get_tlv_offset_by_id((void *)inbuff, inbuff_len, 0x13);
      if (offset > 0) {
        struct qmi_generic_uint32_t_tlv *pdp_instance =
            (struct qmi_generic_uint32_t_tlv *)(inbuff + offset);
        server->PDP_Session[sim_slot].pdp_instance_id = pdp_instance->data;
        g_print(" - PDP Instance ID: %u\n", pdp_res->pdp_instance.data);
      }
      request_network_start(sim_slot);

      /* QMI Response */
      pdp_res->response.result_code_type = 0x02;
      pdp_res->response.generic_result_size = 0x04;
      pdp_res->response.result = 0x00;
      pdp_res->response.response = 0x00;

      /* PDP identifier. Unsure if this is arbitrary or if I'm missing
         something in libqmi that gives me this? */
      pdp_res->pdp_id.id = 0x10;   // TLV ID
      pdp_res->pdp_id.len = 0x01;  // u8
      pdp_res->pdp_id.data = server->PDP_Session[sim_slot].internal_pdp_id; // This is a local ID we pass around
      /* We should have one PDP ID for each active SIM slot with IMS enabled */
     
      /* These go back as they came in */
      pdp_res->pdp_sequence.id = 0x11;  // TLV ID
      pdp_res->pdp_sequence.len = 0x04; // u32
      pdp_res->pdp_sequence.data = server->PDP_Session[sim_slot].pdp_sequence_id;

      /* PDP Instance ID */
      pdp_res->pdp_instance.id = 0x12;  // TLV ID
      pdp_res->pdp_instance.len = 0x04; // u32
      pdp_res->pdp_instance.data = server->PDP_Session[sim_slot].pdp_instance_id;

      outbuff_len += sizeof(struct ims_dcm_pdp_activate_response);
      break;
    case IMS_DCM_LINK_ADD:
      g_print("[DCM] Add Link Request!\n");
      response = (struct qmi_generic_result_ind *)(outbuff + outbuff_len);
      response->result_code_type = 0x02;
      response->generic_result_size = 0x04;
      response->result = 0x00;
      response->response = 0x00;
      outbuff_len += sizeof(struct qmi_generic_result_ind);

      break;
    case IMS_DCM_REGISTER:
      g_print("[DCM] DCM Register request!\n");
      response = (struct qmi_generic_result_ind *)(outbuff + outbuff_len);
      response->result_code_type = 0x02;
      response->generic_result_size = 0x04;
      response->result = 0x00;
      response->response = 0x00;
      outbuff_len += sizeof(struct qmi_generic_result_ind);
      break;
    case IMS_DCM_ENABLE_STATUS:
      g_print("[DCM] DCM Enable status request!\n");
      response = (struct qmi_generic_result_ind *)(outbuff + outbuff_len);
      response->result_code_type = 0x02;
      response->generic_result_size = 0x04;
      response->result = 0x00;
      response->response = 0x00;
      outbuff_len += sizeof(struct qmi_generic_result_ind);
      break;
    case IMS_DCM_CLEAR:
      g_print("[DCM] DCM Clear connection request!\n");
      response = (struct qmi_generic_result_ind *)(outbuff + outbuff_len);
      response->result_code_type = 0x02;
      response->generic_result_size = 0x04;
      response->result = 0x00;
      response->response = 0x00;
      outbuff_len += sizeof(struct qmi_generic_result_ind);
      break;
    default:
      g_printerr("[DCM] Unknown message type: %.4x\n",
              get_qmi_message_id(inbuff, inbuff_len));
      if (outbuff)
        free(outbuff);
      return TRUE; // Keep the handler active
    }

  pkt->length = outbuff_len - sizeof(struct qmi_packet);

#ifdef DEBUG_DCM
    g_print("[DCM][DBG] About to send: ");
    for (int i = 0; i < outbuff_len; i++) {
      g_print("%.2x ", outbuff[i]);
    }
    g_print("\n ------- \n");
#endif

    if (get_qmi_message_id(inbuff, inbuff_len) == IMS_DCM_PDP_ACTIVATE) {
      /*if (wds_get_readiness_step() == WDS_CONNECTION_STATE_FINISHED) {
        uint8_t ipaddr[128];
        wds_copy_ip_address(ipaddr);
        notify_pdp_ipaddress_change(ipaddr);
      }*/
      // let's throw an indication after activation response

      /*
      We need to kickstart a connection here
      */
    }
    sendto(server->sock_fd, outbuff, outbuff_len, 0,
           (struct sockaddr *)&client_addr, addr_len);
  }

  if (outbuff)
    free(outbuff);
  return TRUE; // Keep the handler active
}

gpointer initialize_qmi_server(gpointer user_data) {

  server = g_new0(QRTRServer, 1);
  server->imsd_runtime = (IMSD_Runtime *)user_data;
  server->clients = g_hash_table_new_full(
        (GHashFunc)g_int_hash,
        (GEqualFunc)g_int_equal,
        g_free,
        g_free);
  server->transaction_id = 99;
  // Create QRTR socket
  server->sock_fd = socket(AF_QIPCRTR, SOCK_DGRAM, 0);
  if (server->sock_fd < 0) {
    g_print("[DCM] Failed to create QRTR socket");
    exit(EXIT_FAILURE);
  }

  g_print("[DCM] QRTR server bound to service ID %d, instance ID %d\n",
          SERVICE_ID, INSTANCE_ID);

  // Publish the service
  if (qrtr_publish(server->sock_fd, SERVICE_ID, VERSION_ID, INSTANCE_ID) < 0) {
    g_print("[DCM] Failed to publish QRTR service");
    close(server->sock_fd);
    exit(EXIT_FAILURE);
  }

  g_print("[DCM] QRTR service published successfully\n");

  // Wrap QRTR socket in GIOChannel
  GIOChannel *channel = g_io_channel_unix_new(server->sock_fd);
  server->server_loop = g_main_loop_new(NULL, FALSE);

  // Add the channel to the main loop
  g_io_add_watch(channel, G_IO_IN, handle_incoming_qrtr_request, server);

  g_main_loop_run(server->server_loop);

  g_print("QRTR loop continues\n");
  // Cleanup
  g_hash_table_destroy(server->clients);
  g_io_channel_unref(channel);
  close(server->sock_fd);
  g_main_loop_unref(server->server_loop);
  g_print("QRTR server stopped.\n");
  return NULL;
}
