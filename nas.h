/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Copyright (c) 2024, Biktorgj
 */

#ifndef NAS_H
#define NAS_H

#include <glib.h>
#include <gio/gio.h>
#include <glib-unix.h>
#include <libqmi-glib.h>
#include "imsd.h"

G_BEGIN_DECLS

_Network_Provider_Data get_carrier_data();
void nas_start(QmiDevice *device,
                QmiClientNas *client,
                GCancellable *cancellable);

G_END_DECLS

#endif
