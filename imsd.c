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
#include "imsd.h"
#include "qmi-ims-client.h"

/*
 *  The basic idea:
 *
 *   We need a tool which
 *  * [x]    1. Connects to the modem via QRTR / MSMIPC
 *    [x] (default sim only)  2. Gets all sims and their MCC and MNC
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
typedef struct {
  gchar *device_arg;
  gboolean version;
} _CmdLine;

_CmdLine CmdLine;
static GOptionEntry imsd_params[] = {
    {"device", 'd', 0, G_OPTION_ARG_STRING, &CmdLine.device_arg},
    {"version", 'v', 0, G_OPTION_ARG_NONE, &CmdLine.version},
    {NULL}
    };

static gboolean quit_cb(gpointer user_data) {
  g_printerr("Caught signal, shutting down...\n");
  if (cancellable) {
    /* Ignore consecutive requests of cancellation */
    if (!g_cancellable_is_cancelled(cancellable)) {
      g_printerr("Stopping IMSD...\n");
      cancel_connection_manager();
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
  GFile *device_path;
  GOptionContext *context;
  GError *error = NULL;

  context = g_option_context_new("- IMSD configuration params");

  g_option_context_add_main_entries(context, imsd_params, NULL);
  if (!g_option_context_parse(context, &argc, &argv, &error)) {
    g_printerr("Error: %s\n", error->message);
    exit(EXIT_FAILURE);
  }
  g_option_context_free(context);

  if (CmdLine.version) {
    g_printerr("%s version %s\n", PROG_NAME, RELEASE_VER);
    exit(EXIT_SUCCESS);
  }

  /* No device path given? */
  if (!CmdLine.device_arg) {
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
  device_path = g_file_new_for_commandline_arg(CmdLine.device_arg);
  cancellable = g_cancellable_new();

  g_unix_signal_add(SIGTERM, quit_cb, NULL);
  g_unix_signal_add(SIGINT, quit_cb, NULL);

  loop = g_main_loop_new(NULL, FALSE);

  // Let's start here
  /*
    We're hardcoding this to QMI for now
    But we could easily have different codepaths depending
    on the device used
  */
  if (!create_qmi_client_connection(device_path, cancellable))
    return EXIT_FAILURE;

  g_main_loop_run(loop);
  g_main_loop_unref(loop);

  if (cancellable)
    g_object_unref(cancellable);
  close(lockfile);
  unlink(LOCK_FILE);
  
  g_printerr("bye bye!\n");
  return 0;
}