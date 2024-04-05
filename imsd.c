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
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <unistd.h>
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
 *      1. Connects to the modem via either QMI or QRTR
 *      2. Retrieves the current IMS Settings from the modem and stores them
 *      3. Reads the current carrier and checks against its internal DB of the correct settings
 *      4. Deviates or corrects the settings stored in the modem with what they should be
 *      5. Starts IMS Presence, IMS RTP and the telephony server if they are stopped.
 *      6. ???? 
 *
 *
 *
*/
struct {
    uint8_t verbose;
    pthread_t main_thread;
} runtime;

void *imsd_init() {
    fprintf(stdout, "We're in another thread!\n");
    return NULL;
}


int main(int argc, char **argv) {
  int ret, lockfile;
  runtime.verbose = false;
  fprintf(stdout, "IMS Daemon\n");

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
    fprintf(stderr, "%s: OpenQTI is already running, bailing out\n", __func__);
    return -EBUSY;
  }

  fprintf(stdout, "Start our main thread\n");
  if ((ret = pthread_create(&runtime.main_thread, NULL, &imsd_init, NULL))) {
    fprintf(stderr, "Error creating the main thread!\n");
  }

  /* We join our main thread and wait for it to exit */
  pthread_join(runtime.main_thread, NULL);
  
  close(lockfile);
  unlink(LOCK_FILE);
  return 0;
}