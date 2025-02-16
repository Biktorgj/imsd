/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Copyright (c) 2024, Biktorgj
 */
#include "mfs.h"
#include <gio/gio.h>
#include <glib-unix.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <libqmi-glib.h>
#include <libqrtr-glib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
/* Context */
typedef struct {
  QmiDevice *device;
  QmiClientMfs *client;
  GCancellable *cancellable;
  guint8 sub_requested;
} Context;

static Context *ctx;

static void mfs_test_read_ready(QmiClientMfs *client, GAsyncResult *res) {
  QmiMessageMfsRetrieveEfsitemOutput *output;
  GError *error = NULL;
  const gchar *data = NULL;
  guint32 error_code = 0;
  guint16 data_sz = 0;

  output = qmi_client_mfs_retrieve_efsitem_finish(client, res, &error);
  if (!output) {
    g_printerr("MFS read error: operation failed: %s\n", error->message);
    g_error_free(error);
    exit(EXIT_FAILURE);
    return;
  }

  if (!qmi_message_mfs_retrieve_efsitem_output_get_result(output, &error)) {
    g_printerr("MFS read error: couldn't get the file: %s\n", error->message);
    g_error_free(error);
    qmi_message_mfs_retrieve_efsitem_output_unref(output);
    exit(EXIT_FAILURE);

    return;
  } else {
    g_print("MFS: We didn't get an error!\n");
  }

  if (!qmi_message_mfs_retrieve_efsitem_output_get_error_code(output, &error_code, NULL)) {
    g_printerr("Error getting the... error code!\n");
  //  g_error_free(error);
   // qmi_message_mfs_retrieve_efsitem_output_unref(output);
  //  exit(EXIT_FAILURE);
   // return;
  } else {
    g_print("MFS Read Error code: %u: ", error_code);
    if (error_code != 0) {
        switch (error_code) {
            case 1:
                g_print("Not permitted!\n");
                break;
            case 2:
                g_print("404!\n");
                break;
            case 5:
                g_print("Not enough memory!\n");
                break;
            default:
                g_print("I don't know what this is!\n");
                break;
        }
     //   qmi_message_mfs_retrieve_efsitem_output_unref(output);
      //  exit(EXIT_FAILURE);
    }

  }

  if (!qmi_message_mfs_retrieve_efsitem_output_get_data(output, &data_sz, &data, NULL)) {
    g_print("MFS read: We didn't get any content back?: %u\n", data_sz);
  } else {

    g_print("WE GOT SOMETHING BACK MOTHERFUCKER!\n\n%s\n", data);

    for (int i = 0; i < data_sz; i++) {
      printf(" %.2x ", data[i]);
    }
    printf("\n");
  }
  qmi_message_mfs_retrieve_efsitem_output_unref(output);
  exit(EXIT_FAILURE);
}

static void mfs_test_write_ready(QmiClientMfs *client, GAsyncResult *res) {
  QmiMessageMfsStoreEfsitemOutput *output;
  GError *error = NULL;

  output = qmi_client_mfs_store_efsitem_finish(client, res, &error);
  if (!output) {
    g_printerr("MFS Store error: operation failed: %s\n", error->message);
    g_error_free(error);
    exit(EXIT_FAILURE);

    return;
  }

  if (!qmi_message_mfs_store_efsitem_output_get_result(output, &error)) {
    g_printerr("MFS Store error: couldn't save the file: %s\n", error->message);
    g_error_free(error);
    qmi_message_mfs_store_efsitem_output_unref(output);
    exit(EXIT_FAILURE);

    return;
  } else {
    g_print("MFS Store: Finishing -> Nobody complained here, unref!\n");
  }

  guint32 error_code;
  if (!qmi_message_mfs_store_efsitem_output_get_error_code(output, &error_code, NULL)) {
    g_printerr("Error getting the... error code!\n");
 // g_error_free(error);
 //   qmi_message_mfs_store_efsitem_output_unref(output);
 //   exit(EXIT_FAILURE);
 //   return;
  } else {
    g_print("MFS WRITE Error code: %u: ", error_code);
    if (error_code != 0) {
        switch (error_code) {
            case 1:
                g_print("Not permitted!\n");
                break;
            case 2:
                g_print("404!\n");
                break;
            case 5:
                g_print("Not enough memory!\n");
                break;
            default:
                g_print("I don't know what this is!\n");
                break;
        }
        qmi_message_mfs_store_efsitem_output_unref(output);
        exit(EXIT_FAILURE);
    }

  }
  qmi_message_mfs_store_efsitem_output_unref(output);
  g_print("WRITE READY FINISHED!!\n");
}

void mfs_test_read(char *path) {
  g_print("%s: Read test!\n", __func__);
  guint16 file_sz = 17;//128;
  QmiMessageMfsRetrieveEfsitemInput *input;
  input = qmi_message_mfs_retrieve_efsitem_input_new();
  g_print("Attempting to read file %s...\n", path);

  qmi_message_mfs_retrieve_efsitem_input_set_path(input, path, NULL);
  qmi_message_mfs_retrieve_efsitem_input_set_size(input, file_sz, NULL);
  qmi_client_mfs_retrieve_efsitem(QMI_CLIENT_MFS(ctx->client), input, 10, NULL,
                                  (GAsyncReadyCallback)mfs_test_read_ready,
                                  NULL);
}

void mfs_test_write(char *path) {
  g_print("%s: Write test!\n", __func__);
  char efs_file_data[] = "Yippee Ki‐Yay!";
  gsize data_size = sizeof(efs_file_data) / sizeof(efs_file_data[0]);
  GArray *garray = g_array_sized_new(FALSE, FALSE, sizeof(guint8), data_size);
  g_array_append_vals(garray, efs_file_data, data_size);
    
  QmiMessageMfsStoreEfsitemInput *input;
  input = qmi_message_mfs_store_efsitem_input_new();
  qmi_message_mfs_store_efsitem_input_set_path(input, path, NULL);
  qmi_message_mfs_store_efsitem_input_set_data(input, garray, NULL);
  qmi_message_mfs_store_efsitem_input_set_flags(input, 0x08, NULL);
  qmi_message_mfs_store_efsitem_input_set_permissions(input, 0xff, NULL);
  g_print("Attempting to write %lu bytes to %s\n", sizeof(efs_file_data), path);
  qmi_client_mfs_store_efsitem(QMI_CLIENT_MFS(ctx->client), input, 10, NULL,
                               (GAsyncReadyCallback)mfs_test_write_ready, NULL);
}

void mfs_allocate(QmiDevice *device, QmiClientMfs *client,
                  GCancellable *cancellable) {


  /* Initialize context */
  ctx = g_slice_new(Context);
  ctx->device = g_object_ref(device);
  ctx->client = g_object_ref(client);
  ctx->cancellable = g_object_ref(cancellable);
  g_print("MFS allocated... is it?\n");
  
 char efs_file_path[] = "/nv/item_files/csd/csd.conf";
 char efs_write_path[] = "/nv/item_files/csd/csd.conf";

  mfs_test_write(efs_write_path);
  mfs_test_read(efs_file_path);
}
