#define CFG_STR_MAX_LEN 512

typedef struct cfg_item cfg_item;
typedef cfg_item* cfg_item_t;

struct cfg_item {
    cfg_item_t prev;
    char key[CFG_STR_MAX_LEN];
    char value[CFG_STR_MAX_LEN];
};

cfg_item_t read_config_file(char* path) {
    FILE* fp;
    
    if ((fp = fopen(path, "r+")) == NULL) {
        perror("fopen()");
        return NULL;
    }
    
    cfg_item_t last_co_addr = NULL;
    
    while(1) {
        cfg_item_t co = NULL;
        if ((co = calloc(1, sizeof(cfg_item))) == NULL)
            continue;
        memset(co, 0, sizeof(cfg_item));
        co->prev = last_co_addr;
        
        if (fscanf(fp, "%s = %s", &co->key[0], &co->value[0]) != 2) {
            if (feof(fp)) {
                break;
            }
            if (co->key[0] == '#' || co->key[0] == ';') {
                while (fgetc(fp) != '\n') {
                    // Do nothing (to move the cursor to the end of the line).
                }
                free(co);
                continue;
            }
            perror("fscanf()");
            free(co);
            continue;
        }
        //printf("Key: %s\nValue: %s\n", co->key, co->value);
        last_co_addr = co;
    }
    return last_co_addr;
}
