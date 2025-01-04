/* SPDX-License-Identifier: MIT */

#ifndef QMI_UTIL_H
#define QMI_UTIL_H
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include <stdint.h>

#define QMI_RESULT_SUCCESS 0x0000
#define QMI_RESULT_FAILURE 0x0001
#define QMI_RESULT_UNKNOWN 0x0002

struct qmi_generic_result_ind {
  uint8_t result_code_type;     // 0x02
  uint16_t generic_result_size; // 0x04 0x00
  uint16_t result;
  uint16_t response;
} __attribute__((packed));

/* "Normal QMI message, QMUX header indicates the service this message is for "*/
struct qmi_packet { // 7 byte
  uint8_t message_type;    // Request / Response / Indication
  uint16_t transaction_id; // QMI Transaction ID
  uint16_t msgid;          // QMI Message ID
  uint16_t length;         // QMI Packet size
} __attribute__((packed));

/* There's a lot of trash here that I will remove
   Just picking what I want from the modem distro
   as helpers and the rest will be cleaned up later
 */

struct qmi_generic_uch_arr {
  uint8_t id;
  uint16_t len;
  uint8_t *data[0];
} __attribute__((packed));


struct qmi_generic_uint8_t_tlv {
  uint8_t id;
  uint16_t len; //1
  uint8_t data;
} __attribute__((packed));

struct qmi_generic_uint16_t_tlv {
  uint8_t id;
  uint16_t len; //2
  uint16_t data;
} __attribute__((packed));

struct qmi_generic_uint32_t_tlv {
  uint8_t id;
  uint16_t len; //4
  uint32_t data;
} __attribute__((packed));

struct tlv_header {
  uint8_t id;   // 0x00
  uint16_t len; // 0x02 0x00
} __attribute__((packed));

struct signal_quality_tlv {
  uint8_t id;   // 0x10 = CDMA, 0x11 HDR, 0x12 GSM, 0x13 WCDMA, 0x14 LTE
  uint16_t len; // We only care about the RSSI, but there's more stuff in here
  uint8_t signal_level; // RSSI
} __attribute__((packed));

struct nas_signal_lev {
  /* QMI header */
  struct qmi_packet qmipkt;
  /* Operation result */
  struct qmi_generic_result_ind result;
  /* Signal level data */
  struct signal_quality_tlv signal;

} __attribute__((packed));

struct empty_tlv {
  uint8_t id;
  uint16_t len;
  uint8_t data[0];
} __attribute__((packed));

struct tlv_position {
  uint8_t id;
  uint32_t offset;
  uint16_t size;
};

uint16_t get_qmi_message_id(void *bytes, size_t len);
uint16_t get_qmi_message_type(void *bytes, size_t len);
uint16_t get_qmi_transaction_id(void *bytes, size_t len);
uint16_t get_transaction_id(void *bytes, size_t len);
uint16_t get_tlv_offset_by_id(uint8_t *bytes, size_t len, uint8_t tlvid);
uint16_t did_qmi_op_fail(uint8_t *bytes, size_t len);
int build_qmi_header(void *output, size_t output_len, uint8_t message_type, uint16_t transaction_id, uint16_t message_id);
int build_u8_tlv(void *output, size_t output_len, size_t offset, uint8_t id, uint8_t data);
int build_u32_tlv(void *output, size_t output_len, size_t offset, uint8_t id,
                 uint32_t data);
uint16_t count_tlvs_in_message(uint8_t *bytes, size_t len);
#endif