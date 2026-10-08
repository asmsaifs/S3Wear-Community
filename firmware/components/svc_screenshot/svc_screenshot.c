#include "svc_screenshot.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "s3w_sha256.h"
#include "svc_link.h"
#include "svc_worker.h"
#include "png_enc.h"
#include "s3w_lvgl_port.h"
#include "svc_storage.h"
#include "svc_time.h"

static const char *TAG = "svc_screenshot";

#define CAPTURE_TIMEOUT_MS 1000
#define NAME_PREFIX        "shot_"
#define NAME_SUFFIX        ".png"

const char *svc_screenshot_dir(void)
{
    return svc_storage_sd_check() ? SVC_SCREENSHOT_DIR_SD : SVC_SCREENSHOT_DIR_FLASH;
}

static int sink_file(void *ctx, const uint8_t *data, size_t len)
{
    return fwrite(data, 1, len, (FILE *)ctx) == len ? 0 : 1;
}

static void make_stamp(char stamp[24])
{
    if (svc_time_is_valid()) {
        const time_t now = time(NULL);
        struct tm lt;
        localtime_r(&now, &lt);
        strftime(stamp, 24, "%Y%m%d_%H%M%S", &lt);
    } else {
        snprintf(stamp, 24, "u%lu", (unsigned long)(esp_timer_get_time() / 1000000));
    }
}

static void make_name(char *out, size_t n, const char *dir)
{
    char stamp[24];
    make_stamp(stamp);
    snprintf(out, n, "%s/" NAME_PREFIX "%s" NAME_SUFFIX, dir, stamp);
    for (int i = 2; i < 100 && access(out, F_OK) == 0; i++) { // two shots in one second
        snprintf(out, n, "%s/" NAME_PREFIX "%s_%d" NAME_SUFFIX, dir, stamp, i);
    }
}

static int cmp_name(const void *a, const void *b)
{
    return strcmp(a, b);
}

// /flash only: delete the oldest by name until SVC_SCREENSHOT_FLASH_KEEP are left.
static void prune_flash(void)
{
    enum { MAX_SEEN = 64, NAME_MAX_LEN = 40 };
    static char names[MAX_SEEN][NAME_MAX_LEN]; // called from one task at a time (take() is serial)
    DIR *d = opendir(SVC_SCREENSHOT_DIR_FLASH);
    if (!d) {
        return;
    }
    int n = 0;
    for (struct dirent *e; (e = readdir(d)) != NULL && n < MAX_SEEN;) {
        const size_t l = strlen(e->d_name);
        if (l < NAME_MAX_LEN && strncmp(e->d_name, NAME_PREFIX, strlen(NAME_PREFIX)) == 0 && l > strlen(NAME_SUFFIX) &&
            strcmp(e->d_name + l - strlen(NAME_SUFFIX), NAME_SUFFIX) == 0) {
            strcpy(names[n++], e->d_name);
        }
    }
    closedir(d);
    qsort(names, n, NAME_MAX_LEN, cmp_name);
    for (int i = 0; i < n - SVC_SCREENSHOT_FLASH_KEEP; i++) {
        char path[sizeof SVC_SCREENSHOT_DIR_FLASH + NAME_MAX_LEN];
        snprintf(path, sizeof path, "%s/%.*s", SVC_SCREENSHOT_DIR_FLASH, NAME_MAX_LEN - 1, names[i]);
        ESP_LOGI(TAG, "dropped %s", names[i]);
        unlink(path);
    }
}

esp_err_t svc_screenshot_take(char *path_out, size_t path_len)
{
    lv_display_t *disp = s3w_lvgl_port_display();
    ESP_RETURN_ON_FALSE(disp, ESP_ERR_INVALID_STATE, TAG, "no display");
    const char *dir = svc_screenshot_dir();
    const bool on_flash = strcmp(dir, SVC_SCREENSHOT_DIR_FLASH) == 0;
    if (on_flash) {
        uint64_t total = 0, used = 0;
        const bool known = svc_storage_usage(SVC_STORAGE_FLASH_PATH, &total, &used) == ESP_OK;
        ESP_RETURN_ON_FALSE(known && total - used > SVC_SCREENSHOT_RESERVE_FLASH, ESP_ERR_NO_MEM, TAG,
                            "/flash is full");
    }
    if (mkdir(dir, 0775) != 0 && errno != EEXIST) {
        ESP_LOGE(TAG, "mkdir %s: errno %d", dir, errno);
        return ESP_FAIL;
    }

    const uint32_t w = (uint32_t)lv_display_get_horizontal_resolution(disp);
    const uint32_t h = (uint32_t)lv_display_get_vertical_resolution(disp);
    uint16_t *frame = heap_caps_malloc((size_t)w * h * 2, MALLOC_CAP_SPIRAM);
    ESP_RETURN_ON_FALSE(frame, ESP_ERR_NO_MEM, TAG, "no %u B capture buffer", (unsigned)(w * h * 2));
    esp_err_t err = s3w_lvgl_port_capture(frame, CAPTURE_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "capture: %s", esp_err_to_name(err));
        free(frame);
        return err;
    }

    char path[96], tmp[sizeof path + 8];
    make_name(path, sizeof path, dir);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) {
        ESP_LOGE(TAG, "create %s: errno %d", tmp, errno);
        free(frame);
        return ESP_FAIL;
    }
    const int enc = png_encode_rgb565(frame, w, h, sink_file, f);
    free(frame);
    const bool closed = fclose(f) == 0;
    if (enc != 0 || !closed || rename(tmp, path) != 0) {
        ESP_LOGE(TAG, "write %s failed (encode %d, close %d)", path, enc, closed);
        unlink(tmp);
        return ESP_FAIL;
    }
    struct stat st;
    ESP_LOGI(TAG, "saved %s (%ld bytes)", path, stat(path, &st) == 0 ? (long)st.st_size : -1L);
    if (on_flash) {
        prune_flash();
    }
    if (path_out && path_len) {
        snprintf(path_out, path_len, "%s", path);
    }
    return ESP_OK;
}

/* ------------------------------------------------------------- to the phone */

#define PHONE_PNG_MAX (1024 * 1024) // a full-screen noise image is ~620 KB

static struct {
    volatile bool busy; // from the accepted request until the upload is done
    uint8_t *png;       // PSRAM
    size_t len, cap;
    char name[48];
} s_send;

static int sink_mem(void *ctx, const uint8_t *data, size_t len)
{
    (void)ctx;
    if (s_send.len + len > PHONE_PNG_MAX) {
        return 1;
    }
    if (s_send.len + len > s_send.cap) {
        size_t cap = s_send.cap ? s_send.cap * 2 : 64 * 1024;
        while (cap < s_send.len + len) {
            cap *= 2;
        }
        uint8_t *p = heap_caps_realloc(s_send.png, cap, MALLOC_CAP_SPIRAM);
        if (!p) {
            return 1;
        }
        s_send.png = p;
        s_send.cap = cap;
    }
    memcpy(s_send.png + s_send.len, data, len);
    s_send.len += len;
    return 0;
}

static void send_release(void)
{
    heap_caps_free(s_send.png);
    s_send.png = NULL;
    s_send.len = s_send.cap = 0;
    s_send.busy = false;
}

static bool send_read(void *ctx, uint32_t offset, uint8_t *buf, size_t len)
{
    (void)ctx;
    if (!s_send.png || offset + len > s_send.len) {
        return false;
    }
    memcpy(buf, s_send.png + offset, len);
    return true;
}

static void send_done(void *ctx, esp_err_t err, int32_t peer_status)
{
    (void)ctx;
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "%s: on the phone", s_send.name);
    } else {
        ESP_LOGW(TAG, "%s: not sent (%s, phone status %ld)", s_send.name, esp_err_to_name(err), (long)peer_status);
    }
    send_release();
}

// svc_worker: capture, encode into RAM, start the upload (the link task does the rest).
static void send_job(void *ctx)
{
    (void)ctx;
    lv_display_t *disp = s3w_lvgl_port_display();
    const uint32_t w = (uint32_t)lv_display_get_horizontal_resolution(disp);
    const uint32_t h = (uint32_t)lv_display_get_vertical_resolution(disp);
    uint16_t *frame = heap_caps_malloc((size_t)w * h * 2, MALLOC_CAP_SPIRAM);
    if (!frame) {
        ESP_LOGW(TAG, "send: no capture buffer");
        send_release();
        return;
    }
    const esp_err_t err = s3w_lvgl_port_capture(frame, CAPTURE_TIMEOUT_MS);
    const int enc = err == ESP_OK ? png_encode_rgb565(frame, w, h, sink_mem, NULL) : -1;
    free(frame);
    if (err != ESP_OK || enc != 0) {
        ESP_LOGW(TAG, "send: capture %s, encode %d", esp_err_to_name(err), enc);
        send_release();
        return;
    }
    svc_link_upload_t u = {
        .kind = s3w_v1_TransferKind_TRANSFER_SCREENSHOT_UP,
        .size = (uint32_t)s_send.len,
        .read = send_read,
        .done = send_done,
    };
    char stamp[24];
    make_stamp(stamp);
    snprintf(s_send.name, sizeof s_send.name, NAME_PREFIX "%s" NAME_SUFFIX, stamp);
    strlcpy(u.name, s_send.name, sizeof u.name);
    s3w_sha256(s_send.png, s_send.len, u.sha256);
    const esp_err_t up = svc_link_upload(&u);
    if (up != ESP_OK) {
        ESP_LOGW(TAG, "send: upload %s", esp_err_to_name(up));
        send_release();
        return;
    }
    ESP_LOGI(TAG, "sending %s (%u bytes)", s_send.name, (unsigned)s_send.len);
}

esp_err_t svc_screenshot_send(void)
{
    ESP_RETURN_ON_FALSE(svc_link_is_up(), ESP_ERR_INVALID_STATE, TAG, "no phone");
    ESP_RETURN_ON_FALSE(s3w_lvgl_port_output_is_on(), ESP_ERR_INVALID_STATE, TAG, "screen off");
    ESP_RETURN_ON_FALSE(!s_send.busy, ESP_ERR_NOT_FINISHED, TAG, "busy");
    s_send.busy = true;
    const esp_err_t err = svc_worker_submit(send_job, NULL);
    if (err != ESP_OK) {
        s_send.busy = false;
    }
    return err;
}

static void on_request(void *ctx, const s3w_v1_Envelope *req)
{
    (void)ctx;
    const esp_err_t err = svc_screenshot_send();
    svc_link_reply_status(req,
                          err == ESP_OK                  ? s3w_v1_StatusCode_STATUS_OK
                          : err == ESP_ERR_NOT_FINISHED  ? s3w_v1_StatusCode_STATUS_BUSY
                          : err == ESP_ERR_INVALID_STATE ? s3w_v1_StatusCode_STATUS_INVALID
                                                         : s3w_v1_StatusCode_STATUS_INTERNAL,
                          err == ESP_OK ? NULL : esp_err_to_name(err));
}

esp_err_t svc_screenshot_start(void)
{
    return svc_link_register(s3w_v1_Envelope_screenshot_tag, on_request, NULL);
}
