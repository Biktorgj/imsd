/* SPDX-License-Identifier: GPL-3 */

/****************************
 * open                 _   *
 * (_)                 | |  *
 *  _ _ __ ___  ___  __| |  *
 * | | '_ ` _ \/ __|/ _` |  *
 * | | | | | | \__ \ (_| |  *
 * |_|_| |_| |_|___/\__,_|  *
 *                          *
 *        version 0.0.5     *
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
#include "dcm.h"
#include "imsd.h"
#include "qmi-ims-client.h"
#include "config.h"

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

IMSD_Runtime *runtime;

static GOptionEntry imsd_params[] = {
    {"device", 'd', 0, G_OPTION_ARG_STRING, &CmdLine.device_arg},
    {"version", 'v', 0, G_OPTION_ARG_NONE, &CmdLine.version},
    {NULL}};

// static gboolean imsd_quit_callback(gpointer user_data) {
//   g_printerr("[IMSD] Caught signal, shutting down...\n");
//   if (cancellable) {
     /* Ignore consecutive requests of cancellation */
//     if (!g_cancellable_is_cancelled(cancellable)) {
//       g_printerr("[IMSD] Stopping client services...\n");
//       cancel_connection_manager();
//       g_cancellable_cancel(cancellable);
//     }
//   }
//   g_printerr("Exiting!\n");
//   if (loop)
//     g_idle_add((GSourceFunc)g_main_loop_quit, loop);
//   else
//     exit(0);

//   return FALSE;
// }


int load_main_config() {
    // Load main configuration
    IMSD_Config mainConfig = {0};
    load_config(CONFIG_FILE, &mainConfig);

    printf("IMSD Base config:\n");
    printf("  Phone Model: %s\n", mainConfig.phone_model);
    printf("  SIM Slots: %u\n", mainConfig.sim_slots);
    printf("  MCFG Configuration files path: %s\n", mainConfig.mcfg_config_path);
    printf("  Fallback APN if no compatible MCFG files are found: %s\n", mainConfig.fallback_apn);
    printf("  Needs custom VoLTE Mixers: %i\n", mainConfig.uses_custom_volte_mixers);
    printf("  Custom playback Mixers: %s\n", mainConfig.playback_mixers);
    printf("  Custom Recording Mixers: %s\n", mainConfig.recording_mixers);
    return 0;
}

int main(int argc, char **argv) {
  int lockfile;
  fprintf(stdout, "IMS Daemon %s \n", RELEASE_VER);
  GOptionContext *context;
  GError *error = NULL;

  context = g_option_context_new("- IMSD configuration params");

  g_option_context_add_main_entries(context, imsd_params, NULL);
  if (!g_option_context_parse(context, &argc, &argv, &error)) {
    g_printerr("Error: %s\n", error->message);
    return -EINVAL;
  }
  g_option_context_free(context);

  if (CmdLine.version) {
    g_printerr("%s version %s\n", PROG_NAME, RELEASE_VER);
    return 0;
  }

  /* No device path given? */
  if (!CmdLine.device_arg) {
    g_printerr("error: no device path specified\n");
    return -EINVAL;
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
  load_main_config();
  /* Create a slice for the runtime */
  runtime = g_new(IMSD_Runtime, 1);
  
  /* Build new GFile from the commandline arg */
  runtime->client_path = g_file_new_for_commandline_arg(CmdLine.device_arg);
  runtime->cancellable = g_cancellable_new();
  g_mutex_init(&runtime->mutex);
  g_cond_init(&runtime->cond);

  /*
   * Here's where we start:
   *  1. We kickstart a series of QMI clients while
   *  2. We start an independent QMI server to answer
   *     the modem
   */

  GThread *qmi_client = g_thread_new("QMI Client", initialize_qmi_client, runtime);
  GThread *qmi_server = g_thread_new("QMI Server", initialize_qmi_server, runtime);


  loop = g_main_loop_new(NULL, FALSE);


  // Wait for threads to finish
  g_thread_join(qmi_client);
  g_thread_join(qmi_server);
  // g_thread_join(consumer);
  g_main_loop_run(loop);

  g_main_loop_unref(loop);

  if (cancellable)
    g_object_unref(cancellable);
  close(lockfile);
  unlink(LOCK_FILE);

  g_printerr("bye bye!\n");
  return 0;
}