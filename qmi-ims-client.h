/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Copyright (c) 2024, Biktorgj
 */

#ifndef QMI_IMS_CLIENT_H
#define QMI_IMS_CLIENT_H

#include <glib.h>
#include <gio/gio.h>
#include <glib-unix.h>

G_BEGIN_DECLS
void get_autoconnect_settings();

void cancel_connection_manager();
void release_clients ();
gboolean create_qmi_client_connection(GFile *file, GCancellable *cancellable);

G_END_DECLS

#endif