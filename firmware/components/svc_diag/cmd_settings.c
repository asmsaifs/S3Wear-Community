// Console: `settings list|get|set|reset|factory` — svc_settings (P2-02).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_console.h"
#include "esp_err.h"
#include "svc_diag_priv.h"
#include "svc_settings.h"

static void print_one(s3w_setting_t id)
{
    const s3w_setting_info_t *in = settings_info(id);
    char buf[S3W_SETTINGS_STR_POOL];
    switch (in->type) {
    case S3W_SETTING_TYPE_INT:
        printf("%-16s int  %ld  (default %ld, %ld..%ld)\n", in->key, (long)svc_settings_get_int(id), (long)in->def,
               (long)in->min, (long)in->max);
        break;
    case S3W_SETTING_TYPE_BOOL:
        printf("%-16s bool %d  (default %d)\n", in->key, svc_settings_get_bool(id), (int)in->def);
        break;
    default:
        svc_settings_get_str(id, buf, sizeof buf);
        printf("%-16s str  \"%s\"  (default \"%s\", max %ld)\n", in->key, buf, in->def_str, (long)in->max);
        break;
    }
}

static int find(const char *key, s3w_setting_t *id)
{
    if (settings_find(key, id) != ESP_OK) {
        printf("unknown key '%s' (see 'settings list')\n", key);
        return 1;
    }
    return 0;
}

static int set(s3w_setting_t id, const char *val)
{
    const s3w_setting_info_t *in = settings_info(id);
    esp_err_t err;
    char *end = NULL;
    if (in->type == S3W_SETTING_TYPE_STR) {
        err = svc_settings_set_str(id, val);
    } else {
        const long v = strtol(val, &end, 0);
        if (*val == '\0' || *end != '\0') {
            printf("not a number: '%s'\n", val);
            return 1;
        }
        if (in->type == S3W_SETTING_TYPE_BOOL) {
            err = v == 0 || v == 1 ? svc_settings_set_bool(id, v == 1) : ESP_ERR_INVALID_ARG;
        } else {
            err = svc_settings_set_int(id, (int32_t)v);
        }
    }
    if (err != ESP_OK) {
        printf("rejected: %s\n", esp_err_to_name(err));
        return 1;
    }
    print_one(id);
    return 0;
}

static int cmd_settings(int argc, char **argv)
{
    const char *sub = argc >= 2 ? argv[1] : "";
    s3w_setting_t id;
    if (strcmp(sub, "list") == 0) {
        for (int i = 0; i < S3W_SETTING_COUNT; i++) {
            print_one((s3w_setting_t)i);
        }
        printf("%d pending NVS write(s)\n", svc_settings_pending());
        return 0;
    }
    if (strcmp(sub, "get") == 0 && argc == 3) {
        if (find(argv[2], &id)) {
            return 1;
        }
        print_one(id);
        return 0;
    }
    if (strcmp(sub, "set") == 0 && argc == 4) {
        return find(argv[2], &id) ? 1 : set(id, argv[3]);
    }
    if (strcmp(sub, "reset") == 0 && argc == 3) {
        if (find(argv[2], &id)) {
            return 1;
        }
        svc_settings_reset(id);
        print_one(id);
        return 0;
    }
    if (strcmp(sub, "factory") == 0) {
        const esp_err_t err = svc_settings_factory_reset();
        printf("settings factory reset: %s\n", esp_err_to_name(err));
        return err == ESP_OK ? 0 : 1;
    }
    printf("usage: settings list | get <key> | set <key> <value> | reset <key> | factory\n");
    return 1;
}

esp_err_t diag_register_settings(void)
{
    const esp_console_cmd_t cmd = {
        .command = "settings",
        .help = "svc_settings: list, get <key>, set <key> <value>, reset <key> (to default), factory (all to defaults)",
        .hint = "list|get|set|reset|factory",
        .func = cmd_settings,
    };
    return esp_console_cmd_register(&cmd);
}
