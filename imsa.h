/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Copyright (c) 2024, Biktorgj
 */

#ifndef IMSA_H
#define IMSA_H

#include <glib.h>
#include <gio/gio.h>
#include <glib-unix.h>
#include <libqmi-glib.h>

G_BEGIN_DECLS
void imsa_attempt_bind();
void imsa_start(QmiDevice *device,
                QmiClientImsa *client,
                GCancellable *cancellable);

G_END_DECLS

#endif
