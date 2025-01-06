/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Copyright (c) 2024, Biktorgj
 */

#ifndef QMI_IMS_CLIENT_H
#define QMI_IMS_CLIENT_H

#include <glib.h>
#include <gio/gio.h>
#include <glib-unix.h>
#include <stdint.h>

G_BEGIN_DECLS

enum qmi_ims_service_errors {
    IMS_OPER_NOERR = 0x00000000,
    IMS_OPER_NOT_READY = 0x00000001,
    IMS_OPER_NOT_AVAIL = 0x00000002,
    IMS_OPER_READ_FAIL = 0x00000003,
    IMS_OPER_WRITE_FAIL = 0x00000004,
    IMS_OPER_ERR_UNKNOWN = 0x00000005
};

enum qmi_ims_subscription_type {
    IMS_SUB_NONE = 0xFFFFFFFF,
    IMS_SUB_PRI = 0x00000000,
    IMS_SUB_SEC = 0x00000001,
    IMS_SUB_TER = 0x00000002
};

enum initialization_state {
    IMS_INIT_PENDING = 0,
    IMS_INIT_OK = 1,
    IMS_INIT_ERR = 2
};

void request_network_start(uint32_t sim_slot);

void cancel_connection_manager();
void release_clients ();
gboolean create_qmi_client_connection(GFile *file, GCancellable *cancellable);
gpointer initialize_qmi_client(gpointer user_data);
G_END_DECLS

#endif