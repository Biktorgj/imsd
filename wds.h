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

void wds_start(QmiDevice *device,
                QmiClientWds *client,
                GCancellable *cancellable);
G_END_DECLS

#endif
