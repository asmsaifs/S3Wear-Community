// S3W Link service: see include/svc_link.h. One task owns the reassembler, the request
// tracker and both bulk engines; other tasks hand it work through a queue.
#include "svc_link.h"

#include <stdatomic.h>
#include <string.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "link_bulk.h"
#include "link_frame.h"
#include "link_rpc.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "s3w_event.h"
#include "s3w_task.h"
#include "svc_ble.h"

static const char *TAG = "svc_link";

ESP_EVENT_DEFINE_BASE(SVC_LINK_EVENT);

// Frames from svc_ble wait here while the link task is busy or preempted (svc_power turning
// the screen on for the notification takes ~180 ms). The phone sends a TextBitmap's strips
// back to back: up to 72 KB, ~150 frames of 512 B. A lost frame drops the whole bitmap, so
// the queue holds one in full (items 24 B; frames copied to PSRAM).
#define QUEUE_LEN 160
#define MAX_HANDLERS 24
#define MAX_SINKS 8
#define DEFAULT_WINDOW 16
#define TX_RETRY_MS 5   // svc_ble out of buffers: wait for the controller to drain
#define TX_RETRY_MAX 60 // 300 ms, then give up on the frame

typedef enum {
    ITEM_FRAME,   // from svc_ble: data = frame
    ITEM_STATE,   // link up/down (len = up)
    ITEM_EVENT,   // data = encoded Envelope to send
    ITEM_REQUEST, // data = encoded Envelope with its id; cb/ctx/id
    ITEM_UPLOAD,  // data = svc_link_upload_t
} item_kind_t;

typedef struct {
    uint8_t kind;
    uint8_t chan;
    uint16_t len;
    uint32_t id;
    uint8_t *data;
    svc_link_reply_cb_t cb;
    void *ctx;
} item_t;

typedef struct {
    pb_size_t tag;
    svc_link_handler_t fn;
    void *ctx;
} handler_t;

typedef struct {
    uint32_t kind;
    svc_link_xfer_sink_t sink;
    void *ctx;
} sink_entry_t;

typedef struct {
    svc_link_reply_cb_t cb;
    void *ctx;
} req_ctx_t;

typedef struct {
    bool active;
    bool begun; // TransferBegin answered, chunks flowing
    svc_link_upload_t up;
    uint32_t id;
    uint32_t chunk;
    link_bulk_tx_t tx;
} upload_t;

static struct {
    QueueHandle_t q;
    s3w_mutex_t lock; // handler / sink tables and request ids; never held across a callback
    bool started;
    volatile bool online; // written by the link task only

    handler_t handlers[MAX_HANDLERS];
    sink_entry_t sinks[MAX_SINKS];
    uint8_t n_handlers, n_sinks;

    // Link-task state.
    link_reasm_t reasm;
    uint8_t *reasm_buf;
    link_seq_t ctrl_seq, bulk_seq;
    link_rpc_t rpc;
    bool mismatch_reported;
    uint32_t next_xfer_id;

    // In PSRAM (svc_link_start): an Envelope is over 8 KB since AppIcon / TextBitmap (P4-07).
    s3w_v1_Envelope *env;     // decoded incoming message
    s3w_v1_Envelope *scratch; // outgoing messages the service builds itself (link task only)
    uint8_t *enc;             // s3w_v1_Envelope_size bytes
    const s3w_v1_Envelope *cur_req; // request being handled (reply allowed)

    link_bulk_rx_t rx;
    sink_entry_t rx_sink_copy; // the sink of the running transfer
    upload_t up;

    svc_link_status_t st;
} s;

static void *rpc_alloc(size_t n)
{
    return heap_caps_malloc(n, MALLOC_CAP_8BIT);
}

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* ----------------------------------------------------------------- sending */

// One ATT payload on TX or BULK. NO_MEM: the controller is busy, wait a moment (this is the
// link task, not the UI).
static bool ble_tx(void *ctx, link_chan_t chan, const uint8_t *frame, size_t len)
{
    (void)ctx;
    for (int i = 0; i < TX_RETRY_MAX; i++) {
        const esp_err_t err = svc_ble_send(chan == LINK_CHAN_BULK ? SVC_BLE_CHAN_BULK : SVC_BLE_CHAN_CONTROL, frame, len);
        if (err == ESP_OK) {
            return true;
        }
        if (err != ESP_ERR_NO_MEM) {
            ESP_LOGD(TAG, "tx: %s", esp_err_to_name(err));
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(TX_RETRY_MS));
    }
    ESP_LOGW(TAG, "tx: out of buffers");
    return false;
}

static bool send_msg(const uint8_t *msg, size_t len)
{
    const uint16_t mtu = svc_ble_mtu();
    s.st.mtu = mtu;
    if (mtu < 3 + LINK_MIN_FRAME) {
        return false;
    }
    if (!link_ctrl_send(&s.ctrl_seq, msg, len, link_max_frame(mtu), ble_tx, NULL)) {
        return false;
    }
    s.st.tx_messages++;
    return true;
}

static bool rpc_tx(void *ctx, const uint8_t *msg, size_t len)
{
    (void)ctx;
    return send_msg(msg, len);
}

static bool encode(const s3w_v1_Envelope *env, uint8_t *buf, size_t cap, size_t *len)
{
    pb_ostream_t os = pb_ostream_from_buffer(buf, cap);
    if (!pb_encode(&os, s3w_v1_Envelope_fields, env)) {
        ESP_LOGW(TAG, "encode: %s", PB_GET_ERROR(&os));
        return false;
    }
    *len = os.bytes_written;
    return true;
}

// Link task only: encode into the shared buffer and send as an event.
static bool send_env(const s3w_v1_Envelope *env)
{
    size_t len;
    return encode(env, s.enc, s3w_v1_Envelope_size, &len) && send_msg(s.enc, len);
}

static void fill_status(s3w_v1_Status *st, int32_t code, const char *msg)
{
    memset(st, 0, sizeof *st);
    st->code = code;
    if (msg) {
        strlcpy(st->message, msg, sizeof st->message);
    }
}

esp_err_t svc_link_reply(const s3w_v1_Envelope *req, s3w_v1_Envelope *reply)
{
    ESP_RETURN_ON_FALSE(req == s.cur_req && req->id != 0, ESP_ERR_INVALID_STATE, TAG, "reply outside its handler");
    reply->id = 0;
    reply->reply_to = req->id;
    size_t len;
    ESP_RETURN_ON_FALSE(encode(reply, s.enc, s3w_v1_Envelope_size, &len), ESP_ERR_INVALID_SIZE, TAG, "encode");
    link_rpc_cache_reply(&s.rpc, req->id, s.enc, len);
    return send_msg(s.enc, len) ? ESP_OK : ESP_FAIL;
}

esp_err_t svc_link_reply_status(const s3w_v1_Envelope *req, int32_t code, const char *msg)
{
    // Not on the stack: an Envelope is over 1 KB.
    s3w_v1_Envelope *r = s.scratch;
    *r = (s3w_v1_Envelope)s3w_v1_Envelope_init_zero;
    r->has_status = true;
    fill_status(&r->status, code, msg);
    r->which_body = s3w_v1_Envelope_ack_tag;
    return svc_link_reply(req, r);
}

/* ---------------------------------------------------- phone → watch transfers */

static bool sink_write(void *ctx, uint32_t offset, const uint8_t *data, size_t len)
{
    (void)ctx;
    if (!s.rx_sink_copy.sink.write(s.rx_sink_copy.ctx, offset, data, len)) {
        return false;
    }
    s.st.bulk_rx_bytes += (uint32_t)len;
    return true;
}

static bool sink_finish(void *ctx)
{
    (void)ctx;
    return s.rx_sink_copy.sink.finish ? s.rx_sink_copy.sink.finish(s.rx_sink_copy.ctx) : true;
}

static void send_xfer_status(uint32_t id, uint32_t next_offset)
{
    s3w_v1_Envelope *e = s.scratch;
    *e = (s3w_v1_Envelope)s3w_v1_Envelope_init_zero;
    e->which_body = s3w_v1_Envelope_xfer_status_tag;
    e->body.xfer_status.transfer_id = id;
    e->body.xfer_status.next_offset = next_offset;
    send_env(e);
}

static void send_xfer_end(uint32_t id, bool verified, int32_t code)
{
    s3w_v1_Envelope *e = s.scratch;
    *e = (s3w_v1_Envelope)s3w_v1_Envelope_init_zero;
    e->which_body = s3w_v1_Envelope_xfer_end_tag;
    e->body.xfer_end.transfer_id = id;
    e->body.xfer_end.verified = verified;
    if (code != s3w_v1_StatusCode_STATUS_OK) {
        e->body.xfer_end.has_status = true;
        e->body.xfer_end.status.code = code;
    }
    send_env(e);
}

static void rx_abort(const char *why)
{
    if (s.rx.state == LINK_BULK_RX_RECEIVING) {
        ESP_LOGW(TAG, "transfer %lu aborted: %s", (unsigned long)s.rx.id, why);
        link_bulk_rx_abort(&s.rx);
        if (s.rx_sink_copy.sink.abort) {
            s.rx_sink_copy.sink.abort(s.rx_sink_copy.ctx);
        }
    }
    s.st.xfer_in = false;
}

// Reply to TransferBegin: status in both the Envelope and the TransferStatus.
static void reply_xfer(const s3w_v1_Envelope *req, uint32_t id, uint32_t next_offset, int32_t code, const char *msg)
{
    s3w_v1_Envelope *r = s.scratch;
    *r = (s3w_v1_Envelope)s3w_v1_Envelope_init_zero;
    r->has_status = true;
    fill_status(&r->status, code, msg);
    r->which_body = s3w_v1_Envelope_xfer_status_tag;
    r->body.xfer_status.transfer_id = id;
    r->body.xfer_status.next_offset = next_offset;
    r->body.xfer_status.has_status = true;
    r->body.xfer_status.status = r->status;
    svc_link_reply(req, r);
}

static void on_xfer_begin(const s3w_v1_Envelope *req)
{
    const s3w_v1_TransferBegin *b = &req->body.xfer_begin;
    const uint32_t window = b->window ? b->window : DEFAULT_WINDOW;

    sink_entry_t entry;
    bool found = false;
    s3w_mutex_lock(s.lock, UINT32_MAX);
    for (int i = 0; i < s.n_sinks; i++) {
        if (s.sinks[i].kind == b->kind) {
            entry = s.sinks[i];
            found = true;
            break;
        }
    }
    s3w_mutex_unlock(s.lock);

    if (!found) {
        reply_xfer(req, b->transfer_id, 0, s3w_v1_StatusCode_STATUS_UNSUPPORTED, "no sink for this kind");
        return;
    }
    if (s.rx.state == LINK_BULK_RX_RECEIVING) {
        reply_xfer(req, b->transfer_id, 0, s3w_v1_StatusCode_STATUS_BUSY, "transfer in progress");
        return;
    }
    if (window > LINK_BULK_WINDOW_MAX) {
        reply_xfer(req, b->transfer_id, 0, s3w_v1_StatusCode_STATUS_INVALID, "window");
        return;
    }

    uint32_t resume = 0;
    const int32_t code = entry.sink.open ? entry.sink.open(entry.ctx, b, &resume) : s3w_v1_StatusCode_STATUS_OK;
    if (code != s3w_v1_StatusCode_STATUS_OK) {
        reply_xfer(req, b->transfer_id, 0, code, "refused");
        return;
    }
    s.rx_sink_copy = entry; // sinks may be re-registered while a transfer runs
    const link_bulk_sink_t sink = {.write = sink_write, .finish = sink_finish, .ctx = NULL};
    if (!link_bulk_rx_begin(&s.rx, b->transfer_id, b->size, window, resume, &sink)) {
        if (entry.sink.abort) {
            entry.sink.abort(entry.ctx);
        }
        reply_xfer(req, b->transfer_id, 0, s3w_v1_StatusCode_STATUS_INVALID, "window or resume offset");
        return;
    }
    s.st.xfer_in = s.rx.state == LINK_BULK_RX_RECEIVING;
    ESP_LOGI(TAG, "transfer %lu: kind %lu, %lu bytes, window %lu, resume at %lu", (unsigned long)b->transfer_id,
             (unsigned long)b->kind, (unsigned long)b->size, (unsigned long)window, (unsigned long)resume);
    reply_xfer(req, b->transfer_id, link_bulk_rx_next(&s.rx), s3w_v1_StatusCode_STATUS_OK, NULL);
    if (s.rx.state == LINK_BULK_RX_DONE) { // nothing left to receive
        send_xfer_end(b->transfer_id, s.rx.verified, s3w_v1_StatusCode_STATUS_OK);
        s.st.xfer_in = false;
    }
}

static void on_bulk_frame(const link_frame_t *f)
{
    link_bulk_chunk_t ch;
    if (!link_bulk_parse(f, &ch)) {
        return;
    }
    // Dead or finished transfers: chunks of the id still answer (TransferEnd replay); others vanish.
    const link_bulk_rx_event_t ev = link_bulk_rx_chunk(&s.rx, ch.transfer_id, ch.offset, ch.data, ch.len);
    switch (ev) {
    case LINK_BULK_RX_NONE:
        break;
    case LINK_BULK_RX_ACK:
        send_xfer_status(s.rx.id, link_bulk_rx_next(&s.rx));
        break;
    case LINK_BULK_RX_COMPLETE:
        ESP_LOGI(TAG, "transfer %lu complete, %s", (unsigned long)s.rx.id, s.rx.verified ? "verified" : "REJECTED");
        send_xfer_end(s.rx.id, s.rx.verified, s.rx.verified ? s3w_v1_StatusCode_STATUS_OK : s3w_v1_StatusCode_STATUS_INVALID);
        s.st.xfer_in = false;
        break;
    case LINK_BULK_RX_ERROR:
        ESP_LOGW(TAG, "transfer %lu: storage failed at %lu", (unsigned long)s.rx.id, (unsigned long)link_bulk_rx_next(&s.rx));
        send_xfer_end(s.rx.id, false, s3w_v1_StatusCode_STATUS_NO_SPACE);
        if (s.rx_sink_copy.sink.abort) {
            s.rx_sink_copy.sink.abort(s.rx_sink_copy.ctx);
        }
        s.st.xfer_in = false;
        break;
    }
}

/* ---------------------------------------------------- watch → phone transfers */

static void upload_finish(esp_err_t err, int32_t peer_status)
{
    if (!s.up.active) {
        return;
    }
    s.up.active = false;
    s.st.xfer_out = false;
    ESP_LOGI(TAG, "upload %lu finished: %s", (unsigned long)s.up.id, esp_err_to_name(err));
    if (s.up.up.done) {
        s.up.up.done(s.up.up.ctx, err, peer_status);
    }
}

static void on_upload_begin_reply(void *user, link_rpc_result_t res, const void *reply)
{
    (void)user;
    if (!s.up.active || s.up.begun) {
        return;
    }
    if (res != LINK_RPC_OK) {
        upload_finish(res == LINK_RPC_TIMEOUT ? ESP_ERR_TIMEOUT : ESP_ERR_INVALID_STATE, 0);
        return;
    }
    const s3w_v1_Envelope *e = reply;
    if (e->has_status && e->status.code != s3w_v1_StatusCode_STATUS_OK) {
        upload_finish(ESP_FAIL, e->status.code);
        return;
    }
    if (e->which_body != s3w_v1_Envelope_xfer_status_tag || e->body.xfer_status.transfer_id != s.up.id) {
        upload_finish(ESP_FAIL, s3w_v1_StatusCode_STATUS_INVALID);
        return;
    }
    if (!link_bulk_tx_begin(&s.up.tx, s.up.id, s.up.up.size, DEFAULT_WINDOW, s.up.chunk, e->body.xfer_status.next_offset, now_ms())) {
        upload_finish(ESP_FAIL, s3w_v1_StatusCode_STATUS_INVALID);
        return;
    }
    s.up.begun = true;
}

static void start_upload(const svc_link_upload_t *u)
{
    if (s.up.active) {
        if (u->done) {
            u->done(u->ctx, ESP_ERR_INVALID_STATE, s3w_v1_StatusCode_STATUS_BUSY);
        }
        return;
    }
    const uint16_t mtu = svc_ble_mtu();
    if (!s.online || mtu < 3 + LINK_HDR_LEN + LINK_BULK_HDR_LEN + 16) {
        if (u->done) {
            u->done(u->ctx, ESP_ERR_INVALID_STATE, 0);
        }
        return;
    }
    memset(&s.up, 0, sizeof s.up);
    s.up.up = *u;
    s.up.active = true;
    s.up.id = s.next_xfer_id++;
    s.up.chunk = link_max_frame(mtu) - LINK_HDR_LEN - LINK_BULK_HDR_LEN;
    s.st.xfer_out = true;

    s3w_v1_Envelope *e = s.scratch;
    *e = (s3w_v1_Envelope)s3w_v1_Envelope_init_zero;
    s3w_mutex_lock(s.lock, UINT32_MAX);
    e->id = link_rpc_next_id(&s.rpc);
    s3w_mutex_unlock(s.lock);
    e->which_body = s3w_v1_Envelope_xfer_begin_tag;
    s3w_v1_TransferBegin *b = &e->body.xfer_begin;
    b->transfer_id = s.up.id;
    b->kind = u->kind;
    b->size = u->size;
    memcpy(b->sha256, u->sha256, sizeof b->sha256);
    strlcpy(b->name, u->name, sizeof b->name);
    b->window = DEFAULT_WINDOW;
    b->chunk_size = s.up.chunk;
    size_t len;
    if (!encode(e, s.enc, s3w_v1_Envelope_size, &len) ||
        !link_rpc_start(&s.rpc, e->id, s.enc, len, now_ms(), on_upload_begin_reply, NULL)) {
        upload_finish(ESP_FAIL, 0);
    }
}

// Put chunks on the wire while the engine has them: one per call so acks are never starved.
static void pump_upload(void)
{
    if (!s.up.active || !s.up.begun) {
        return;
    }
    uint32_t off;
    size_t len;
    if (link_bulk_tx_next(&s.up.tx, &off, &len)) {
        static uint8_t frame[LINK_HDR_LEN + LINK_BULK_HDR_LEN + 514];
        static uint8_t data[514];
        if (len > sizeof data || !s.up.up.read(s.up.up.ctx, off, data, len)) {
            ESP_LOGW(TAG, "upload %lu: read failed at %lu", (unsigned long)s.up.id, (unsigned long)off);
            upload_finish(ESP_FAIL, s3w_v1_StatusCode_STATUS_INTERNAL);
            return;
        }
        const link_seq_t keep = s.bulk_seq;
        const size_t n = link_bulk_build(frame, sizeof frame, &s.bulk_seq, s.up.id, off, data, len);
        if (n == 0 || !ble_tx(NULL, LINK_CHAN_BULK, frame, n)) {
            s.bulk_seq = keep;
            if (!s.online) {
                upload_finish(ESP_ERR_INVALID_STATE, 0);
            }
            return; // retry on the next pass; the ack timeout bounds this
        }
        s.st.bulk_tx_bytes += (uint32_t)len;
        link_bulk_tx_sent(&s.up.tx, now_ms());
    }
    link_bulk_tx_poll(&s.up.tx, now_ms());
    if (s.up.tx.state == LINK_BULK_TX_DONE) {
        upload_finish(ESP_OK, 0);
    } else if (s.up.tx.state == LINK_BULK_TX_FAILED) {
        upload_finish(ESP_ERR_TIMEOUT, 0);
    }
}

static void on_xfer_status_event(const s3w_v1_Envelope *e)
{
    if (s.up.active && s.up.begun && e->body.xfer_status.transfer_id == s.up.id) {
        link_bulk_tx_on_status(&s.up.tx, e->body.xfer_status.next_offset, now_ms());
    }
}

static void on_xfer_end_event(const s3w_v1_Envelope *e)
{
    const s3w_v1_TransferEnd *t = &e->body.xfer_end;
    if (s.up.active && s.up.begun && t->transfer_id == s.up.id) {
        link_bulk_tx_on_end(&s.up.tx, t->verified);
        upload_finish(t->verified ? ESP_OK : ESP_FAIL, t->has_status ? t->status.code : 0);
    }
}

/* ---------------------------------------------------------------- dispatch */

static void dispatch_body(const s3w_v1_Envelope *env)
{
    switch (env->which_body) {
    case s3w_v1_Envelope_xfer_begin_tag:
        on_xfer_begin(env);
        return;
    case s3w_v1_Envelope_xfer_status_tag:
        on_xfer_status_event(env);
        return;
    case s3w_v1_Envelope_xfer_end_tag:
        on_xfer_end_event(env);
        return;
    default:
        break;
    }

    handler_t h = {0};
    s3w_mutex_lock(s.lock, UINT32_MAX);
    for (int i = 0; i < s.n_handlers; i++) {
        if (s.handlers[i].tag == env->which_body) {
            h = s.handlers[i];
            break;
        }
    }
    s3w_mutex_unlock(s.lock);

    if (h.fn) {
        h.fn(h.ctx, env);
    } else if (env->id) {
        ESP_LOGD(TAG, "no handler for body %u", env->which_body);
        svc_link_reply_status(env, s3w_v1_StatusCode_STATUS_UNSUPPORTED, "unsupported");
    } else {
        ESP_LOGD(TAG, "event body %u ignored", env->which_body);
    }
}

static void on_control_message(const uint8_t *msg, size_t len)
{
    s3w_v1_Envelope *env = s.env;
    *env = (s3w_v1_Envelope)s3w_v1_Envelope_init_zero;
    pb_istream_t is = pb_istream_from_buffer(msg, len);
    if (!pb_decode(&is, s3w_v1_Envelope_fields, env)) {
        // Too long a string or malformed: the watch rejects it (docs/06 §7).
        ESP_LOGW(TAG, "bad envelope (%u bytes): %s", (unsigned)len, PB_GET_ERROR(&is));
        s.st.rx_dropped++;
        if (env->id != 0 && env->reply_to == 0) {
            s.cur_req = env;
            svc_link_reply_status(env, s3w_v1_StatusCode_STATUS_INVALID, "bad message");
            s.cur_req = NULL;
        }
        return;
    }
    s.st.rx_messages++;

    if (env->reply_to != 0) {
        if (!link_rpc_on_reply(&s.rpc, env->reply_to, env)) {
            ESP_LOGD(TAG, "late reply to %lu", (unsigned long)env->reply_to);
        }
        return;
    }
    if (env->id != 0) {
        const uint8_t *cached;
        size_t cached_len;
        switch (link_rpc_on_request(&s.rpc, env->id, &cached, &cached_len)) {
        case LINK_SEEN_REPLAY:
            ESP_LOGD(TAG, "request %lu again: resending reply", (unsigned long)env->id);
            send_msg(cached, cached_len);
            return;
        case LINK_SEEN_IGNORE:
            return;
        case LINK_SEEN_NEW:
            break;
        }
    }
    s.cur_req = env;
    dispatch_body(env);
    s.cur_req = NULL;
}

// Frame from another protocol major: tell the phone once (docs/06 §7), it will ask for an update.
static void report_version_mismatch(uint8_t version)
{
    if (s.mismatch_reported) {
        return;
    }
    s.mismatch_reported = true;
    ESP_LOGW(TAG, "frame with protocol major %u (we speak %u)", version, LINK_PROTO_MAJOR);
    s3w_v1_Envelope *e = s.scratch;
    *e = (s3w_v1_Envelope)s3w_v1_Envelope_init_zero;
    e->has_status = true;
    fill_status(&e->status, s3w_v1_StatusCode_STATUS_UNSUPPORTED, "protocol major");
    e->which_body = s3w_v1_Envelope_hello_ack_tag;
    e->body.hello_ack.proto_major = LINK_PROTO_MAJOR;
    send_env(e);
}

static void on_frame(link_chan_t chan, const uint8_t *data, size_t len)
{
    link_frame_t f;
    if (!link_frame_parse(data, len, &f)) {
        s.st.rx_dropped++;
        return;
    }
    if (f.chan != chan) {
        s.st.rx_dropped++; // frame header says another channel than the characteristic it came on
        return;
    }
    if (f.version != LINK_PROTO_MAJOR) {
        report_version_mismatch(f.version);
        return;
    }
    if (f.chan == LINK_CHAN_BULK) {
        on_bulk_frame(&f);
        return;
    }
    const uint8_t *msg;
    size_t msg_len;
    const uint32_t dropped_before = s.reasm.dropped;
    if (link_reasm_push(&s.reasm, &f, &msg, &msg_len) == LINK_RX_MESSAGE) {
        on_control_message(msg, msg_len);
    }
    s.st.rx_dropped += s.reasm.dropped - dropped_before;
    s.st.rx_dups = s.reasm.dups;
}

static void on_link_state(bool up)
{
    if (up == s.online) {
        return;
    }
    s.online = up;
    // New session either way: sequence numbers, ids and any transfer start over.
    link_reasm_reset(&s.reasm);
    s.ctrl_seq.next = 0;
    s.bulk_seq.next = 0;
    s.mismatch_reported = false;
    link_rpc_reset(&s.rpc);
    rx_abort("link lost");
    if (s.up.active) {
        upload_finish(ESP_ERR_INVALID_STATE, 0);
    }
    s.st.up = up;
    ESP_LOGI(TAG, "session %s", up ? "up" : "down");
    const svc_link_evt_state_t ev = {.up = up};
    s3w_event_post(SVC_LINK_EVENT, SVC_LINK_EVT_STATE, &ev, sizeof ev);
}

static void req_done(void *user, link_rpc_result_t res, const void *reply)
{
    req_ctx_t *rc = user;
    const svc_link_reply_cb_t cb = rc->cb;
    void *ctx = rc->ctx;
    heap_caps_free(rc);
    if (cb) {
        cb(ctx, res, reply);
    }
}

static void handle_item(item_t *it)
{
    switch (it->kind) {
    case ITEM_FRAME:
        if (s.online) {
            on_frame(it->chan == SVC_BLE_CHAN_BULK ? LINK_CHAN_BULK : LINK_CHAN_CONTROL, it->data, it->len);
        }
        break;
    case ITEM_STATE:
        on_link_state(it->len != 0);
        break;
    case ITEM_EVENT:
        if (s.online && !send_msg(it->data, it->len)) {
            ESP_LOGW(TAG, "event not sent");
        }
        break;
    case ITEM_REQUEST: {
        req_ctx_t *rc = heap_caps_malloc(sizeof *rc, MALLOC_CAP_8BIT);
        bool ok = rc && s.online;
        if (ok) {
            rc->cb = it->cb;
            rc->ctx = it->ctx;
            ok = link_rpc_start(&s.rpc, it->id, it->data, it->len, now_ms(), req_done, rc);
        }
        if (!ok) {
            heap_caps_free(rc);
            if (it->cb) {
                it->cb(it->ctx, LINK_RPC_FAILED, NULL);
            }
        }
        break;
    }
    case ITEM_UPLOAD:
        start_upload((const svc_link_upload_t *)it->data);
        break;
    }
    heap_caps_free(it->data);
}

static void link_task(void *arg)
{
    (void)arg;
    for (;;) {
        // Wakes only for queued work or a deadline (request retry, bulk ack timeout); streaming an
        // upload polls with a zero wait so acks interleave with chunks.
        uint32_t wait = link_rpc_poll(&s.rpc, now_ms());
        if (s.up.active && s.up.begun) {
            const uint32_t w = s.up.tx.state == LINK_BULK_TX_SENDING ? 0 : link_bulk_tx_poll(&s.up.tx, now_ms());
            if (w < wait) {
                wait = w;
            }
        }
        item_t it;
        const TickType_t ticks = wait == UINT32_MAX ? portMAX_DELAY : (wait == 0 ? 0 : pdMS_TO_TICKS(wait) + 1);
        while (xQueueReceive(s.q, &it, ticks) == pdTRUE) {
            handle_item(&it);
            if (uxQueueMessagesWaiting(s.q) == 0) {
                break;
            }
        }
        pump_upload();
    }
}

/* -------------------------------------------------------------- public API */

static void on_ble_rx(svc_ble_chan_t chan, const uint8_t *data, size_t len, void *ctx)
{
    (void)ctx; // NimBLE host task: copy and return
    if (len == 0 || len > 0xFFFF) {
        return;
    }
    uint8_t *copy = heap_caps_malloc_prefer(len, 2, MALLOC_CAP_SPIRAM, MALLOC_CAP_8BIT);
    if (copy) {
        memcpy(copy, data, len);
    }
    item_t it = {.kind = ITEM_FRAME, .chan = (uint8_t)chan, .len = (uint16_t)len, .data = copy};
    if (!copy || xQueueSend(s.q, &it, 0) != pdTRUE) {
        heap_caps_free(copy);
        s.st.queue_full++;
    }
}

static void on_ble_event(void *ctx, esp_event_base_t base, int32_t id, const void *data, size_t len)
{
    (void)ctx;
    (void)base;
    if (id != SVC_BLE_EVT_STATE || len < sizeof(svc_ble_evt_state_t)) {
        return;
    }
    const bool up = ((const svc_ble_evt_state_t *)data)->state == SVC_BLE_STATE_SECURED;
    const item_t it = {.kind = ITEM_STATE, .len = up};
    // Blocking briefly is fine here (event task); a lost state change would wedge the session.
    if (xQueueSend(s.q, &it, pdMS_TO_TICKS(100)) != pdTRUE) {
        s.st.queue_full++;
        ESP_LOGE(TAG, "state change lost");
    }
}

esp_err_t svc_link_start(void)
{
    ESP_RETURN_ON_FALSE(!s.started, ESP_ERR_INVALID_STATE, TAG, "started");
    s.q = xQueueCreate(QUEUE_LEN, sizeof(item_t));
    s.lock = s3w_mutex_create();
    // 16 KB reassembly buffer in PSRAM (docs/02: allocations > 4 KB).
    s.reasm_buf = heap_caps_malloc(LINK_CTRL_MAX_MSG, MALLOC_CAP_SPIRAM);
    s.env = heap_caps_malloc(sizeof *s.env, MALLOC_CAP_SPIRAM);
    s.scratch = heap_caps_malloc(sizeof *s.scratch, MALLOC_CAP_SPIRAM);
    s.enc = heap_caps_malloc(s3w_v1_Envelope_size, MALLOC_CAP_SPIRAM);
    ESP_RETURN_ON_FALSE(s.q && s.lock && s.reasm_buf && s.env && s.scratch && s.enc, ESP_ERR_NO_MEM, TAG, "alloc");
    link_reasm_init(&s.reasm, s.reasm_buf, LINK_CTRL_MAX_MSG);
    const link_rpc_mem_t mem = {.alloc = rpc_alloc, .release = heap_caps_free};
    link_rpc_init(&s.rpc, mem, rpc_tx, NULL);
    s.next_xfer_id = 1;

    const s3w_task_cfg_t task = {
        .name = "svc_link",
        .fn = link_task,
        .stack_bytes = S3W_STACK_LINK,
        .prio = S3W_PRIO_LINK,
        .core = S3W_CORE_SERVICES, // internal stack: transfer sinks write flash
    };
    ESP_RETURN_ON_ERROR(s3w_task_create(&task, NULL), TAG, "task");
    s.started = true;
    svc_ble_set_rx(on_ble_rx, NULL);
    ESP_RETURN_ON_ERROR(s3w_event_subscribe(SVC_BLE_EVENT, SVC_BLE_EVT_STATE, on_ble_event, NULL, NULL), TAG, "ble");

    // svc_ble may already be connected (unlikely this early): pick up its state.
    svc_ble_status_t st;
    svc_ble_get_status(&st);
    if (st.state == SVC_BLE_STATE_SECURED) {
        const svc_ble_evt_state_t e = {.state = st.state};
        on_ble_event(NULL, SVC_BLE_EVENT, SVC_BLE_EVT_STATE, &e, sizeof e);
    }
    ESP_LOGI(TAG, "started");
    return ESP_OK;
}

bool svc_link_is_up(void)
{
    return s.online;
}

esp_err_t svc_link_register(pb_size_t body_tag, svc_link_handler_t handler, void *ctx)
{
    ESP_RETURN_ON_FALSE(s.lock && handler && body_tag != 0, ESP_ERR_INVALID_STATE, TAG, "register");
    esp_err_t err = ESP_OK;
    s3w_mutex_lock(s.lock, UINT32_MAX);
    for (int i = 0; i < s.n_handlers; i++) {
        if (s.handlers[i].tag == body_tag) {
            err = ESP_ERR_INVALID_STATE;
        }
    }
    if (err == ESP_OK && s.n_handlers == MAX_HANDLERS) {
        err = ESP_ERR_NO_MEM;
    }
    if (err == ESP_OK) {
        s.handlers[s.n_handlers++] = (handler_t){.tag = body_tag, .fn = handler, .ctx = ctx};
    }
    s3w_mutex_unlock(s.lock);
    return err;
}

esp_err_t svc_link_xfer_register(uint32_t kind, const svc_link_xfer_sink_t *sink, void *ctx)
{
    ESP_RETURN_ON_FALSE(s.lock && sink && sink->open && sink->write, ESP_ERR_INVALID_ARG, TAG, "sink");
    esp_err_t err = ESP_OK;
    s3w_mutex_lock(s.lock, UINT32_MAX);
    for (int i = 0; i < s.n_sinks; i++) {
        if (s.sinks[i].kind == kind) {
            err = ESP_ERR_INVALID_STATE;
        }
    }
    if (err == ESP_OK && s.n_sinks == MAX_SINKS) {
        err = ESP_ERR_NO_MEM;
    }
    if (err == ESP_OK) {
        s.sinks[s.n_sinks++] = (sink_entry_t){.kind = kind, .sink = *sink, .ctx = ctx};
    }
    s3w_mutex_unlock(s.lock);
    return err;
}

// Encode on the caller's task into a heap buffer and queue it for the link task.
static esp_err_t queue_envelope(item_t *it, const s3w_v1_Envelope *env)
{
    ESP_RETURN_ON_FALSE(s.started && s.online, ESP_ERR_INVALID_STATE, TAG, "link down");
    pb_ostream_t sz = PB_OSTREAM_SIZING;
    ESP_RETURN_ON_FALSE(pb_encode(&sz, s3w_v1_Envelope_fields, env), ESP_ERR_INVALID_ARG, TAG, "encode");
    ESP_RETURN_ON_FALSE(sz.bytes_written <= LINK_CTRL_MAX_MSG, ESP_ERR_INVALID_SIZE, TAG, "too big");
    uint8_t *buf = heap_caps_malloc(sz.bytes_written, MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(buf, ESP_ERR_NO_MEM, TAG, "heap");
    pb_ostream_t os = pb_ostream_from_buffer(buf, sz.bytes_written);
    if (!pb_encode(&os, s3w_v1_Envelope_fields, env)) {
        heap_caps_free(buf);
        return ESP_ERR_INVALID_ARG;
    }
    it->data = buf;
    it->len = (uint16_t)os.bytes_written; // ≤ 16 KB, fits
    if (xQueueSend(s.q, it, 0) != pdTRUE) {
        heap_caps_free(buf);
        s.st.queue_full++;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t svc_link_send(s3w_v1_Envelope *env)
{
    env->id = 0;
    env->reply_to = 0;
    item_t it = {.kind = ITEM_EVENT};
    return queue_envelope(&it, env);
}

esp_err_t svc_link_request(s3w_v1_Envelope *env, svc_link_reply_cb_t cb, void *ctx)
{
    ESP_RETURN_ON_FALSE(s.started, ESP_ERR_INVALID_STATE, TAG, "not started");
    s3w_mutex_lock(s.lock, UINT32_MAX);
    env->id = link_rpc_next_id(&s.rpc);
    s3w_mutex_unlock(s.lock);
    env->reply_to = 0;
    item_t it = {.kind = ITEM_REQUEST, .id = env->id, .cb = cb, .ctx = ctx};
    return queue_envelope(&it, env);
}

esp_err_t svc_link_upload(const svc_link_upload_t *up)
{
    ESP_RETURN_ON_FALSE(s.started && s.online, ESP_ERR_INVALID_STATE, TAG, "link down");
    ESP_RETURN_ON_FALSE(up && up->read, ESP_ERR_INVALID_ARG, TAG, "read");
    svc_link_upload_t *copy = heap_caps_malloc(sizeof *copy, MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(copy, ESP_ERR_NO_MEM, TAG, "heap");
    *copy = *up;
    const item_t it = {.kind = ITEM_UPLOAD, .data = (uint8_t *)copy};
    if (xQueueSend(s.q, &it, 0) != pdTRUE) {
        heap_caps_free(copy);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void svc_link_get_status(svc_link_status_t *out)
{
    *out = s.st;
    out->up = s.online;
    out->rpc_retries = s.rpc.retries;
    out->rpc_timeouts = s.rpc.timeouts;
}
