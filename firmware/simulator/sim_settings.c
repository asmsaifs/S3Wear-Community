#include "sim_settings.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "modes.h"
#include "settings_apps.h"
#include "settings_store.h"
#include "sim_battery.h"
#include "sim_modes.h"
#include "ui_nav.h"

static settings_store_t s_store;

static void apply(s3w_setting_t id)
{
    switch (id) {
    case S3W_SETTING_TIME_24H:
        ui_clock_set_24h(settings_store_get_bool(&s_store, id));
        break;
    case S3W_SETTING_DND_DAYS:
    case S3W_SETTING_DND_START:
    case S3W_SETTING_DND_END: {
        const mode_sched_t s = {
            .days = (uint8_t)settings_store_get_int(&s_store, S3W_SETTING_DND_DAYS),
            .start_min = (uint16_t)settings_store_get_int(&s_store, S3W_SETTING_DND_START),
            .end_min = (uint16_t)settings_store_get_int(&s_store, S3W_SETTING_DND_END),
        };
        sim_modes_sched(MODE_DND, &s);
        break;
    }
    case S3W_SETTING_SLEEP_DAYS:
    case S3W_SETTING_SLEEP_START:
    case S3W_SETTING_SLEEP_END: {
        const mode_sched_t s = {
            .days = (uint8_t)settings_store_get_int(&s_store, S3W_SETTING_SLEEP_DAYS),
            .start_min = (uint16_t)settings_store_get_int(&s_store, S3W_SETTING_SLEEP_START),
            .end_min = (uint16_t)settings_store_get_int(&s_store, S3W_SETTING_SLEEP_END),
        };
        sim_modes_sched(MODE_SLEEP, &s);
        break;
    }
    case S3W_SETTING_BATTERY_SAVER:
        sim_battery_set_saver(settings_store_get_bool(&s_store, id));
        break;
    default:
        break;
    }
}

static int32_t be_get_int(s3w_setting_t id, void *ctx)
{
    (void)ctx;
    if (id == S3W_SETTING_BATTERY_SAVER) {
        return sim_battery_saver();
    }
    return settings_info(id)->type == S3W_SETTING_TYPE_BOOL ? settings_store_get_bool(&s_store, id)
                                                           : settings_store_get_int(&s_store, id);
}

static esp_err_t be_set_int(s3w_setting_t id, int32_t v, void *ctx)
{
    (void)ctx;
    const esp_err_t err = settings_info(id)->type == S3W_SETTING_TYPE_BOOL
                              ? settings_store_set_bool(&s_store, id, v != 0, NULL)
                              : settings_store_set_int(&s_store, id, v, NULL);
    if (err == ESP_OK) {
        printf("setting: %s = %" PRId32 "\n", settings_info(id)->key, v);
        apply(id);
    }
    return err;
}

static void be_get_str(s3w_setting_t id, char *buf, size_t len, void *ctx)
{
    (void)ctx;
    snprintf(buf, len, "%s", settings_store_get_str(&s_store, id));
}

static esp_err_t be_set_str(s3w_setting_t id, const char *v, void *ctx)
{
    (void)ctx;
    const esp_err_t err = settings_store_set_str(&s_store, id, v, NULL);
    if (err == ESP_OK) {
        printf("setting: %s = %s\n", settings_info(id)->key, v);
        apply(id);
    }
    return err;
}

static esp_err_t be_action(settings_action_t a, void *ctx)
{
    (void)ctx;
    static const char *const names[] = {"restart", "power off", "factory reset"};
    printf("settings action: %s\n", names[a]);
    if (a == SETTINGS_ACT_FACTORY_RESET) {
        settings_store_defaults(&s_store);
        for (int i = 0; i < S3W_SETTING_COUNT; i++) {
            apply((s3w_setting_t)i);
        }
    }
    return ESP_OK;
}

static void be_about(settings_about_t *out, void *ctx)
{
    (void)ctx;
    snprintf(out->version, sizeof out->version, "0.3.0-sim");
    snprintf(out->idf, sizeof out->idf, "v5.5.1");
    snprintf(out->storage, sizeof out->storage, "12.4 of 28 MB free");
}

void sim_settings_init(void)
{
    settings_store_defaults(&s_store);
    const settings_app_backend_t be = {
        .get_int = be_get_int,
        .set_int = be_set_int,
        .get_str = be_get_str,
        .set_str = be_set_str,
        .action = be_action,
        .about = be_about,
    };
    settings_apps_set_backend(&be);
}

bool sim_settings_set(const char *key, const char *value)
{
    s3w_setting_t id;
    if (settings_find(key, &id) != ESP_OK) {
        return false;
    }
    if (settings_info(id)->type == S3W_SETTING_TYPE_STR) {
        return be_set_str(id, value, NULL) == ESP_OK;
    }
    char *end;
    const long v = strtol(value, &end, 10);
    return end != value && *end == '\0' && be_set_int(id, (int32_t)v, NULL) == ESP_OK;
}

bool sim_settings_is(const char *key, const char *value)
{
    s3w_setting_t id;
    if (settings_find(key, &id) != ESP_OK) {
        return false;
    }
    if (settings_info(id)->type == S3W_SETTING_TYPE_STR) {
        return strcmp(settings_store_get_str(&s_store, id), value) == 0;
    }
    char *end;
    const long v = strtol(value, &end, 10);
    return end != value && *end == '\0' && be_get_int(id, NULL) == (int32_t)v;
}
