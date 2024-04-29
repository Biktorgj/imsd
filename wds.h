/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Copyright (c) 2024, Biktorgj
 */

#ifndef WDS_H
#define WDS_H

#include <glib.h>
#include <gio/gio.h>
#include <glib-unix.h>
#include <libqmi-glib.h>

G_BEGIN_DECLS
enum {
    PROFILE_TYPE_3GPP = 0,
    PROFILE_TYPE_3GPP2 = 1,
};

enum {
    PDP_TYPE_IPV4 = 0x00,
    PDP_TYPE_PPP,
    PDP_TYPE_IPV6,
    PDP_TYPE_IPV4V6,
    PDP_TYPE_UNKNOWN = 0x04
};

enum {
    APN_TYPE_MASK_UNKNOWN = 0x00,
    APN_TYPE_MASK_DEFAULT = 0x01,
    APN_TYPE_MASK_IMS = 0x02,
    APN_TYPE_MASK_MMS = 0x04,
    APN_TYPE_MASK_DUN = 0x08,
    APN_TYPE_MASK_SUPL = 0x10,
    APN_TYPE_MASK_HIPRI = 0x20,
    APN_TYPE_MASK_FOTA = 0x40,
    APN_TYPE_MASK_CBS = 0x80,
    APN_TYPE_MASK_IA = 0x100,
    APN_TYPE_MASK_EMERG = 0x200
};

void wds_do_stop_network (gboolean disable_autoconnect);
void wds_start(QmiDevice *device,
                QmiClientWds *client,
                GCancellable *cancellable);
G_END_DECLS

#endif
