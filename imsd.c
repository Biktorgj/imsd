/* SPDX-License-Identifier: GPL-3 */

/****************************
 *  _                   _   *
 * (_)                 | |  *
 *  _ _ __ ___  ___  __| |  *
 * | | '_ ` _ \/ __|/ _` |  *
 * | | | | | | \__ \ (_| |  *
 * |_|_| |_| |_|___/\__,_|  *
 *                          *
 *        version 0.0.1     *
 ****************************/
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <gio/gio.h>
#include <glib-unix.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <libqmi-glib.h>
#include <libqrtr-glib.h>

/* Local includes */
#include "conn-manager.h"
#include "imsd.h"

/*
 *  The basic idea:
 *
 *   We need a tool which
 *      1. Connects to the modem via QRTR / MSMIPC
 *      2. Gets all sims and their MCC and MNC
 *      3. Check if all the IMS services are up in the baseband
 *      4. Foreach, get the profiles associated with those carriers
 *          If settings in baseband differ from from local, update them via IMSS
 *      5. Bring up ims apns via WDS for each sim
 *      6. ???
 *
 *
 *
 */

static GMainLoop *loop = NULL;
static GCancellable *cancellable;

static gchar *device_str;

static GOptionEntry main_entries[] = {
    {"device", 'd', 0, G_OPTION_ARG_STRING, &device_str}};

static gboolean quit_cb(gpointer user_data) {
  g_printerr("Caught signal, shutting down...\n");
  cancel_connection_manager();
  if (cancellable) {
    /* Ignore consecutive requests of cancellation */
    if (!g_cancellable_is_cancelled(cancellable)) {
      g_printerr("Stopping IMSD...\n");
      g_cancellable_cancel(cancellable);
    }
  }
  g_printerr("Exiting!\n");
  if (loop)
    g_idle_add((GSourceFunc)g_main_loop_quit, loop);
  else
    exit(0);

  return FALSE;
}

int main(int argc, char **argv) {
  int lockfile;
  fprintf(stdout, "IMS Daemon %s \n", RELEASE_VER);
  GFile *file;
  GOptionContext *context;
  GError *error = NULL;

  context = g_option_context_new("- IMSD configuration params");

  g_option_context_add_main_entries(context, main_entries, NULL);
  if (!g_option_context_parse(context, &argc, &argv, &error)) {
    g_printerr("Error: %s\n", error->message);
    exit(EXIT_FAILURE);
  }
  g_option_context_free(context);

  /* No device path given? */
  if (!device_str) {
    g_printerr("error: no device path specified\n");
    exit(EXIT_FAILURE);
  }

  /* We shall only start once */
  if ((lockfile = open(LOCK_FILE, O_RDWR | O_CREAT | O_TRUNC, 0660)) == -1) {
    fprintf(stderr, "%s: Can't create the lock file!\n", __func__);
    return -ENOENT;
  }

  if (flock(lockfile, LOCK_EX | LOCK_NB) < 0) {
    fprintf(stderr, "%s is already running!\n", PROG_NAME);
    return -EBUSY;
  }
  
  /* Build new GFile from the commandline arg */
  file = g_file_new_for_commandline_arg(device_str);


  cancellable = g_cancellable_new();

  g_unix_signal_add(SIGTERM, quit_cb, NULL);
  g_unix_signal_add(SIGINT, quit_cb, NULL);

  loop = g_main_loop_new(NULL, FALSE);

  // Let's start here
  if (!create_client_connection (file))
        return EXIT_FAILURE;

  g_main_loop_run(loop);
  g_main_loop_unref(loop);

  if (cancellable)
    g_object_unref(cancellable);
  close(lockfile);
  unlink(LOCK_FILE);
  return 0;
}