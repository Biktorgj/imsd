/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Copyright (c) 2024, Biktorgj
 */

#ifndef PDC_H
#define PDC_H

#include <glib.h>
#include <gio/gio.h>
#include <glib-unix.h>
#include <libqmi-glib.h>

G_BEGIN_DECLS

typedef struct {
    GArray *id;
    QmiPdcConfigurationType config_type;
    guint32 token;
    guint32 version;
    gchar *description;
    guint32 total_size;
} ConfigInfo;

void pdc_start(QmiDevice *device, QmiClientPdc *client,
               GCancellable *cancellable);

G_END_DECLS

#endif
