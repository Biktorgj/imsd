#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdio.h>
#define MAX_CONFIG_STR_LEN 50

typedef struct {
    char phone_model[MAX_CONFIG_STR_LEN];
    uint8_t sim_slots;
    char mcfg_config_path[MAX_CONFIG_STR_LEN];
    char fallback_apn[MAX_CONFIG_STR_LEN];
    uint8_t uses_custom_volte_mixers;
    char playback_mixers[MAX_CONFIG_STR_LEN];
    char recording_mixers[MAX_CONFIG_STR_LEN];
} IMSD_Config;

typedef struct {
    char *id;
    uint8_t type;
    ssize_t sz;
    char *path;
    uint8_t *data;
} NV_Item;

typedef struct {
    char *id;
    uint8_t type;
    ssize_t sz;
    char *path;
    uint8_t *data;
} EFS_Item;


void load_config(const char* filename, IMSD_Config* config);

#endif // CONFIG_H
