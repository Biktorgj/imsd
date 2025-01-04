/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Copyright (c) 2024, Biktorgj
 */

#ifndef DMS_H
#define DMS_H

#include <glib.h>
#include <gio/gio.h>
#include <glib-unix.h>
#include <libqmi-glib.h>

G_BEGIN_DECLS

void dms_start(QmiDevice *device,
                QmiClientDms *client,
                GCancellable *cancellable);

G_END_DECLS

#endif
