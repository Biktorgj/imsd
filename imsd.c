/* SPDX-License-Identifier: GPL-3 */
/****************************
 *  _                   _   *
 * (_)                 | |  *
 *  _ _ __ ___  ___  __| |  *
 * | | '_ ` _ \/ __|/ _` |  *
 * | | | | | | \__ \ (_| |  *
 * |_|_| |_| |_|___/\__,_|  *
 *                          *
 *        version 0.0.0     *
 ****************************/
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <libqmi-glib.h>
#include <libqrtr-glib.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <gio/gio.h>
#include <glib-unix.h>

/* Local includes */
#include "imsd.h"
#include "ims_settings.h"
#include "ims_presence.h"
#include "ims_rtp.h"
#include "ims_video.h"

/*
 *  The basic idea:
 *
 *   We need a tool which 
 *      1. Connects to the modem via QRTR
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
static QmiDevice *device;
static QrtrBus *qrtr_bus;

struct {
    uint8_t verbose;
    pthread_t main_thread;
} runtime;

static gboolean quit_cb(gpointer user_data)
{
    g_info("Caught signal, shutting down...");

    if (loop)
        g_idle_add((GSourceFunc)g_main_loop_quit, loop);
    else
        exit(0);

    return FALSE;
}

int main(int argc, char **argv) {
  int ret, lockfile;
  runtime.verbose = false;
  fprintf(stdout, "IMS Daemon %s %s \n", RELEASE_VER);

  /* Begin */
  while ((ret = getopt(argc, argv, "dvh")) != -1)
    switch (ret) {
    case 'd':
      fprintf(stdout, "- Verbose\n");
      runtime.verbose = true;
      break;

    case 'h':
      fprintf(stdout, "Options:\n");
      fprintf(stdout, " -d: Verbose mode\n");
      fprintf(stdout, " -v: Show version\n");
      fprintf(stdout, " -h: Show this help\n");
      return 0;

    case 'v':
      fprintf(stdout, "IMS daemon version %s \n", RELEASE_VER);
      return 0;

    default:
      break;
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

  g_unix_signal_add(SIGTERM, quit_cb, NULL);
  g_unix_signal_add(SIGINT, quit_cb, NULL);

  loop = g_main_loop_new(NULL, FALSE);

  // Let's start here
  
  g_main_loop_run(loop);
  g_main_loop_unref(loop);

  close(lockfile);
  unlink(LOCK_FILE);
  return 0;
}