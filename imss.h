/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Copyright (c) 2024, Biktorgj
 */

#ifndef IMSS_H
#define IMSS_H

#include <glib.h>
#include <gio/gio.h>
#include <glib-unix.h>
#include <libqmi-glib.h>

G_BEGIN_DECLS

void imss_start(QmiDevice *device,
                QmiClientIms *client,
                GCancellable *cancellable);

G_END_DECLS

#endif
