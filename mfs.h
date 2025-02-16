/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Copyright (c) 2024, Biktorgj
 */

#ifndef MFS_H
#define MFS_H

#include <glib.h>
#include <gio/gio.h>
#include <glib-unix.h>
#include <libqmi-glib.h>

G_BEGIN_DECLS

#define MFS_MAX_PATH 1024
#define MFS_MAX_FILESZ 32768

enum {
    MFS_PERM_ACCESS=0x01,
    MFS_PERM_RO=0x02,
    MFS_PERM_WO=0x04,
    MFS_PERM_RDWR=0x08,
    MFS_PERM_CREATE=0x10,
    MFS_PERM_TRUNCATE=0x80,
    MFS_PERM_APPEND=0x100,
    MFS_PERM_DIR=0x4000,
    MFS_PERM_MKDIR=0x20000,
};

void mfs_test_read();
void mfs_test_write();

void mfs_allocate(QmiDevice *device,
                QmiClientMfs *client,
                GCancellable *cancellable);

G_END_DECLS

#endif
