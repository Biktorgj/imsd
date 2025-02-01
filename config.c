#include "config.h"
#include <ini.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int load_config_handler(void *conf, const char *section,
                               const char *name, const char *value) {
  IMSD_Config *config = (IMSD_Config *)conf;

  if (strcmp(section, "IMSD") == 0) {
    if (strcmp(name, "phone_model") == 0) {
      strncpy(config->phone_model, value, MAX_CONFIG_STR_LEN - 1);
    } else if (strcmp(name, "sim_slots") == 0) {
      config->sim_slots = atoi(value);
    } else if (strcmp(name, "mcfg_config_path") == 0) {
      strncpy(config->mcfg_config_path, value, MAX_CONFIG_STR_LEN - 1);
    } else if (strcmp(name, "fallback_apn") == 0) {
      strncpy(config->fallback_apn, value, MAX_CONFIG_STR_LEN - 1);
    } else if (strcmp(name, "uses_custom_volte_mixers") == 0) {
      config->uses_custom_volte_mixers = atoi(value);
    } else if (strcmp(name, "playback_mixers") == 0) {
      strncpy(config->playback_mixers, value, MAX_CONFIG_STR_LEN - 1);
    } else if (strcmp(name, "recording_mixers") == 0) {
      strncpy(config->recording_mixers, value, MAX_CONFIG_STR_LEN - 1);
    }

    return 1; // Success
  }
  return 0; // Config file doesn't have the IMSD field
}

void load_config(const char *filename, IMSD_Config *config) {
  if (ini_parse(filename, load_config_handler, config) < 0) {
    fprintf(stderr, "Error: Can't load main config file '%s'\n", filename);
    exit(EXIT_FAILURE);
  }
}




