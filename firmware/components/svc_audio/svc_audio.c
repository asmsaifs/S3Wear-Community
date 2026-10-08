// System sounds service (svc_audio.h, docs/03 F15).
//
// Wake-ups of the svc_audio task: requests on its queue (event driven), then while any
// sound plays one 20 ms chunk after another (the I2S write paces the loop), then one
// SVC_AUDIO_IDLE_CLOSE_MS timeout to close the codec. Idle otherwise.
#include "svc_audio.h"

#include <string.h>

#include "bsp_s3w.h"
#include "drv_audio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "s3w_event.h"
#include "s3w_task.h"
#include "svc_modes.h"
#include "svc_power_events.h"
#include "svc_settings.h"

static const char *TAG = "svc_audio";

#define QUEUE_LEN    8
#define CHUNK_MS     20
#define CHUNK_FRAMES (MIX_RATE * CHUNK_MS / 1000)

typedef enum {
    MSG_PLAY,
    MSG_RING,
    MSG_STOP,
    MSG_STOP_ALL,
} msg_type_t;

typedef struct {
    uint8_t type;
    uint8_t id; // snd_id_t
} msg_t;

static struct {
    QueueHandle_t queue;
    SemaphoreHandle_t mutex; // guards st
    svc_audio_status_t st;
    // svc_audio task only
    mix_t mix;
    bool open;
    int volume; // on the codec, -1 = unknown
    // event bus task only
    bool power_seen;
    bool vbus;
} s;

static int16_t s_chunk[CHUNK_FRAMES]; // svc_audio task only

static void stat_add(uint32_t *field)
{
    xSemaphoreTake(s.mutex, portMAX_DELAY);
    (*field)++;
    xSemaphoreGive(s.mutex);
}

static void stat_state(void)
{
    xSemaphoreTake(s.mutex, portMAX_DELAY);
    s.st.codec_open = s.open;
    s.st.playing = mix_active(&s.mix);
    s.st.volume = s.volume;
    xSemaphoreGive(s.mutex);
}

static void codec_close(void)
{
    if (s.open) {
        drv_audio_out_close();
        s.open = false;
        ESP_LOGD(TAG, "codec closed");
    }
    stat_state();
}

static void policy_read(snd_policy_t *p)
{
    modes_state_t ms;
    svc_modes_get(&ms);
    p->silent = svc_settings_get_bool(S3W_SETTING_SILENT);
    p->quiet = ms.quiet;
    p->vol_system = svc_settings_get_int(S3W_SETTING_VOLUME_SYSTEM);
    p->vol_media = svc_settings_get_int(S3W_SETTING_VOLUME_MEDIA);
    p->vol_alarm = svc_settings_get_int(S3W_SETTING_VOLUME_ALARM);
}

static void handle(const msg_t *m)
{
    const snd_id_t id = (snd_id_t)m->id;
    switch ((msg_type_t)m->type) {
    case MSG_PLAY:
    case MSG_RING: {
        snd_policy_t p;
        policy_read(&p);
        const int vol = snd_volume(id, &p);
        if (vol <= 0) {
            stat_add(&s.st.muted);
            ESP_LOGD(TAG, "%s muted", snd_name(id));
            break;
        }
        if (mix_start(&s.mix, id, vol, m->type == MSG_RING ? SVC_AUDIO_RING_RAMP_MS : 0)) {
            stat_add(&s.st.plays);
            ESP_LOGD(TAG, "%s at %d %%", snd_name(id), vol);
        } else {
            stat_add(&s.st.dropped);
        }
        break;
    }
    case MSG_STOP:
        mix_stop(&s.mix, id);
        break;
    case MSG_STOP_ALL:
        mix_stop_all(&s.mix);
        break;
    }
}

// One 20 ms chunk. Returns false if the speaker failed (everything stopped).
static void step(void)
{
    if (!s.open) {
        if (drv_audio_out_open(MIX_RATE, 1) != ESP_OK) {
            ESP_LOGW(TAG, "speaker open failed: sounds dropped");
            mix_stop_all(&s.mix);
            stat_add(&s.st.errors);
            return;
        }
        s.open = true;
        s.volume = -1;
        stat_add(&s.st.opens);
    }
    const int vol = mix_render(&s.mix, s_chunk, CHUNK_FRAMES);
    if (vol != s.volume) {
        drv_audio_set_volume(vol);
        s.volume = vol;
    }
    if (drv_audio_out_write(s_chunk, sizeof s_chunk) != ESP_OK) {
        ESP_LOGW(TAG, "speaker write failed: sounds dropped");
        mix_stop_all(&s.mix);
        stat_add(&s.st.errors);
        codec_close();
    }
}

static void audio_task(void *arg)
{
    (void)arg;
    for (;;) {
        const TickType_t wait =
            mix_active(&s.mix) ? 0 : s.open ? pdMS_TO_TICKS(SVC_AUDIO_IDLE_CLOSE_MS) : portMAX_DELAY;
        msg_t m;
        if (xQueueReceive(s.queue, &m, wait)) {
            handle(&m);
        } else if (!mix_active(&s.mix) && s.open) {
            codec_close(); // idle timeout
        }
        if (mix_active(&s.mix)) {
            step();
        }
        stat_state();
    }
}

// --- Power events: charger and low-battery sounds ----------------------------------------------

static void on_power(void *ctx, esp_event_base_t base, int32_t id, const void *data, size_t len)
{
    (void)ctx;
    (void)base;
    if (id == SVC_POWER_EVT_BATTERY && len >= sizeof(svc_power_battery_t)) {
        const svc_power_battery_t *b = data;
        // The first report is the state at boot, not a plug-in.
        if (s.power_seen && b->vbus && !s.vbus) {
            svc_audio_play(SND_CHARGING);
        }
        s.power_seen = true;
        s.vbus = b->vbus;
    } else if (id == SVC_POWER_EVT_BATTERY_LOW) {
        svc_audio_play(SND_LOW_BATTERY);
    }
}

// --- API --------------------------------------------------------------------------------------

static esp_err_t post(msg_type_t type, snd_id_t id)
{
    ESP_RETURN_ON_FALSE(s.queue, ESP_ERR_INVALID_STATE, TAG, "not started");
    ESP_RETURN_ON_FALSE(id < SND_COUNT || type == MSG_STOP_ALL, ESP_ERR_INVALID_ARG, TAG, "sound");
    if (!s.st.available) {
        return ESP_OK;
    }
    const msg_t m = {.type = (uint8_t)type, .id = (uint8_t)id};
    if (xQueueSend(s.queue, &m, 0) != pdTRUE) {
        stat_add(&s.st.dropped);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t svc_audio_start(void)
{
    ESP_RETURN_ON_FALSE(!s.queue, ESP_ERR_INVALID_STATE, TAG, "already started");
    s.mutex = xSemaphoreCreateMutex();
    s.queue = xQueueCreate(QUEUE_LEN, sizeof(msg_t));
    ESP_RETURN_ON_FALSE(s.mutex && s.queue, ESP_ERR_NO_MEM, TAG, "queue");
    mix_init(&s.mix);
    s.volume = -1;
    s.st.available = bsp_audio_ready();
    if (!s.st.available) {
        ESP_LOGW(TAG, "no speaker: sounds are no-ops");
        return ESP_OK;
    }
    const s3w_task_cfg_t cfg = {.name = "svc_audio",
                                .fn = audio_task,
                                .stack_bytes = 6 * 1024,
                                .prio = S3W_PRIO_AUDIO,
                                .core = S3W_CORE_SERVICES};
    ESP_RETURN_ON_ERROR(s3w_task_create(&cfg, NULL), TAG, "task");
    ESP_RETURN_ON_ERROR(s3w_event_subscribe(SVC_POWER_EVENT, ESP_EVENT_ANY_ID, on_power, NULL, NULL), TAG, "power events");
    ESP_LOGI(TAG, "started");
    return ESP_OK;
}

bool svc_audio_available(void)
{
    return s.st.available;
}

esp_err_t svc_audio_play(snd_id_t id)
{
    return post(MSG_PLAY, id);
}

esp_err_t svc_audio_ring_start(snd_id_t id)
{
    ESP_RETURN_ON_FALSE(snd_loops(id), ESP_ERR_INVALID_ARG, TAG, "not a ringer");
    return post(MSG_RING, id);
}

esp_err_t svc_audio_stop(snd_id_t id)
{
    return post(MSG_STOP, id);
}

esp_err_t svc_audio_stop_all(void)
{
    return post(MSG_STOP_ALL, SND_COUNT);
}

void svc_audio_get_status(svc_audio_status_t *out)
{
    if (!s.mutex) {
        memset(out, 0, sizeof *out);
        return;
    }
    xSemaphoreTake(s.mutex, portMAX_DELAY);
    *out = s.st;
    xSemaphoreGive(s.mutex);
}
