/* SPDX-License-Identifier: GPL-3.0-or-later */

#include <endian.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include "qmi-util.h"
/*
 *
 * Utilities for QMI messages
 *
 */


/* Get Message ID for a QMI message (from a service) */
uint16_t get_qmi_message_id(void *bytes, size_t len) {
  struct qmi_packet *pkt =
      (struct qmi_packet *)(bytes);
  return pkt->msgid;
}

/* Get Message trype for a QMI message (from a service) */
uint16_t get_qmi_message_type(void *bytes, size_t len) {
  struct qmi_packet *pkt =
      (struct qmi_packet *)(bytes);
  return pkt->message_type;
}

/* Get Message ID for a QMI message (from a service) */
uint16_t get_qmi_transaction_id(void *bytes, size_t len) {
  struct qmi_packet *pkt =
      (struct qmi_packet *)(bytes);
  return pkt->transaction_id;
}

/* Get Transaction ID for a QMI message */
uint16_t get_transaction_id(void *bytes, size_t len) {
  struct qmi_packet *pkt = (struct qmi_packet *)bytes;
  return pkt->transaction_id;
}

/* Looks for the request TLV, and if found, it returns the offset
 * so it can be casted later
 * Not all the modem handling daemons send TLVs in the same order,
 * This allows us to avoid having to hardcode every possible combination
 * If invalid, returns 0, which would always be an invalid offset
 */
uint16_t get_tlv_offset_by_id(uint8_t *bytes, size_t len, uint8_t tlvid) {
  uint16_t cur_byte;
  uint8_t *arr = (uint8_t *)bytes;
  struct empty_tlv *this_tlv;
  if (len < sizeof(struct qmi_packet) + 4) {
    printf( "%s: Packet is too small \n", __func__);
    return 0;
  }

  cur_byte = sizeof(struct qmi_packet);
  while ((cur_byte) < len) {
    this_tlv = (struct empty_tlv *)(arr + cur_byte);
    if (this_tlv->id == tlvid) {
      printf(
             "Found TLV with ID 0x%.2x at offset %i with size 0x%.4x\n",
             this_tlv->id, cur_byte, le16toh(this_tlv->len));
      arr = NULL;
      this_tlv = NULL;
      return cur_byte;
    }
    cur_byte += le16toh(this_tlv->len) + sizeof(uint8_t) + sizeof(uint16_t);
    if (cur_byte <= 0 || cur_byte > len) {
      printf( "Current byte is less than or exceeds size\n");
      arr = NULL;
      this_tlv = NULL;
      return -EINVAL;
    }
  }
  arr = NULL;
  this_tlv = NULL;
  return 0;
}

/*
 * Checks if a QMI indication is present in a QMI message (0x02)
 * If there is, returns if the operation succeeded or not, and
 * prints the error response to the log, if any.
 */
uint16_t did_qmi_op_fail(uint8_t *bytes, size_t len) {
  uint16_t result = QMI_RESULT_FAILURE;
  uint16_t cur_byte;
  uint8_t *arr = (uint8_t *)bytes;
  struct qmi_generic_result_ind *this_tlv;

  if (len < sizeof(struct qmi_packet) + 4) {
    printf( "%s: Packet is too small \n", __func__);
    return result;
  }

  cur_byte = sizeof(struct qmi_packet);
  while ((cur_byte) < len) {
    this_tlv = (struct qmi_generic_result_ind *)(arr + cur_byte);
    if (this_tlv->result_code_type == 0x02 &&
        this_tlv->generic_result_size == 0x04) {
      result = this_tlv->result;
      if (this_tlv->result == QMI_RESULT_FAILURE) {
        printf( "** QMI OP Failed: Code 0x%.4x\n",
               this_tlv->response);
      } else {
        printf( "** QMI OP Succeeded: Code 0x%.4x\n",
               this_tlv->response);
      }
      arr = NULL;
      this_tlv = NULL;
      return result;
    }
    cur_byte += le16toh(this_tlv->generic_result_size) + sizeof(uint8_t) +
                sizeof(uint16_t);
    if (cur_byte <= 0 || cur_byte > len) {
      printf( "Current byte is less than or exceeds size\n");
      arr = NULL;
      this_tlv = NULL;
      return result;
    }
  }
  printf( "%s: Couldn't find an indication TLV\n", __func__);
  result = QMI_RESULT_UNKNOWN;

  arr = NULL;
  this_tlv = NULL;
  return result;
}

/*
 * Makes a QMI header for a service (not a control QMI header)
 */
int build_qmi_header(void *output, size_t output_len, uint8_t message_type,
                     uint16_t transaction_id, uint16_t message_id) {
  if (output_len < (sizeof(struct qmi_packet))) {
    printf( "%s: Can't build QMI header, buffer is too small\n",
           __func__);
    return -ENOMEM;
  }
  struct qmi_packet *pkt =
      (struct qmi_packet *)(output);
  pkt->message_type = message_type;
  pkt->transaction_id = transaction_id;
  pkt->msgid = message_id;
  pkt->length =
      output_len - sizeof(struct qmi_packet);
  pkt = NULL;
  return 0;
}

int build_u8_tlv(void *output, size_t output_len, size_t offset, uint8_t id,
                 uint8_t data) {
  if (output_len < offset + sizeof(struct qmi_generic_uint8_t_tlv)) {
    printf( "%s: Can't build U8 TLV, buffer is too small\n",
           __func__);
    return -ENOMEM;
  }
  struct qmi_generic_uint8_t_tlv *pkt =
      (struct qmi_generic_uint8_t_tlv *)(output + offset);
  pkt->id = id;
  pkt->len = 0x01;
  pkt->data = data;
  pkt = NULL;
  return 0;
}

int build_u32_tlv(void *output, size_t output_len, size_t offset, uint8_t id,
                 uint32_t data) {
  if (output_len < offset + sizeof(struct qmi_generic_uint8_t_tlv)) {
    printf( "%s: Can't build U8 TLV, buffer is too small\n",
           __func__);
    return -ENOMEM;
  }
  struct qmi_generic_uint32_t_tlv *pkt =
      (struct qmi_generic_uint32_t_tlv *)(output + offset);
  pkt->id = id;
  pkt->len = 0x04;
  pkt->data = data;
  pkt = NULL;
  return 0;
}
uint16_t count_tlvs_in_message(uint8_t *bytes, size_t len) {
  uint16_t cur_byte;
  uint8_t *arr = (uint8_t *)bytes;
  uint16_t num_tlvs = 0;
  struct empty_tlv *this_tlv;
  if (len < sizeof(struct qmi_packet) + 4) {
    printf( "%s: Packet is too small \n", __func__);
    return 0;
  }

  cur_byte = sizeof(struct qmi_packet);
  while ((cur_byte) < len) {
    this_tlv = (struct empty_tlv *)(arr + cur_byte);
    num_tlvs++;
    cur_byte += le16toh(this_tlv->len) + sizeof(uint8_t) + sizeof(uint16_t);
    if (cur_byte <= 0 || cur_byte > len) {
      printf( "Current byte is less than or exceeds size\n");
      arr = NULL;
      this_tlv = NULL;
      return num_tlvs;
    }
  }
  arr = NULL;
  this_tlv = NULL;
  return num_tlvs;
}
