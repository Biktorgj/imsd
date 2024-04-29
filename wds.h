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
    PROFILE_TYPE_3GPP = 1,
    PROFILE_TYPE_3GPP2 = 0,
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

/* Connection states */
enum {
 WDS_CONNECTION_STATE_NONE = 0,
 WDS_CONNECTION_STATE_PROFILE_READY, // async
 WDS_CONNECTION_STATE_SETUP_DATA_FORMAT, // Multiplexing, async
 WDS_CONNECTION_STATE_SETUP_LINK, // sync
 WDS_CONNECTION_STATE_LINK_BRINGUP, // to actually turn on the netif, sync
 WDS_CONNECTION_STATE_SET_IP_BEARER_METHOD, // Ipv4? 6? Both? Depends on profile config
 WDS_CONNECTION_STATE_BIND_DATA_PORT_IPV4, // Bind mux the data port, async
 WDS_CONNECTION_STATE_SELECT_IP_FAMILY_IPV4, // async
 WDS_CONNECTION_STATE_DO_START_NETWORK_IPV4, // client_wds_start_network, async
 WDS_CONNECTION_STATE_REGISTER_WDS_INDICATIONS_IPV4, // If network started, we need to know if our packet handler dropped
 WDS_CONNECTION_STATE_GET_SETTINGS_IPV4, // Unsure if I need this
 WDS_CONNECTION_STATE_BIND_DATA_PORT_IPV6, // Bind mux the data port, async
 WDS_CONNECTION_STATE_SELECT_IP_FAMILY_IPV6, // async
 WDS_CONNECTION_STATE_DO_START_NETWORK_IPV6,
 WDS_CONNECTION_STATE_ENABLE_INDICATIONS_IPV6,
 WDS_CONNECTION_STATE_GET_SETTINGS_IPV6
};

void wds_do_stop_network (gboolean disable_autoconnect);
void wds_start(QmiDevice *device,
                QmiClientWds *client,
                GCancellable *cancellable);
G_END_DECLS

#endif
