/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Copyright (c) 2024, Biktorgj
 */

#ifndef CONN_MANAGER_H
#define CONN_MANAGER_H

#include <glib.h>
#include <gio/gio.h>
#include <glib-unix.h>

G_BEGIN_DECLS

void get_autoconnect_settings();

void cancel_connection_manager();
void release_clients ();
gboolean create_client_connection(GFile *file);

G_END_DECLS

#endif