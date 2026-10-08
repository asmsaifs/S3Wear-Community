// S3W Link service (docs/06-ble-protocol.md §2, §3, §6; docs/02-firmware-architecture.md §7, P4-02):
// frames from svc_ble become Envelopes dispatched to per-body handlers; requests get replies with
// retries; windowed bulk transfers move files in both directions.
//
// Everything runs on one task ("svc_link"). Handlers and callbacks run on it: they must not
// block (no long flash work, no waiting on other services) and never touch LVGL; post to the UI
// or the event bus instead.
//
// Handlers: register one per Envelope body tag. A handler for a request (req->id != 0) must call
// svc_link_reply() before it returns; for an event (id == 0) there is nothing to answer. A request
// with no handler is answered STATUS_UNSUPPORTED, an event with none is dropped (docs/06 §3). A
// request the peer retries (same id) does not run the handler again: the cached reply is resent.
//
// Transfers: a sink registered for a TransferKind receives phone → watch bulk transfers; the
// service handles Begin / windows / resume / End on the wire. svc_link_upload() does the same
// watch → phone.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "envelope.pb.h"
#include "esp_err.h"
#include "link_rpc.h"
#include "svc_link_events.h"

#ifdef __cplusplus
extern "C" {
#endif

/** After svc_ble_start(). Starts the link task and attaches to svc_ble. */
esp_err_t svc_link_start(void);

/** The companion session is up (secured link, frames flow). */
bool svc_link_is_up(void);

/** Runs on the link task. req is valid only during the call. */
typedef void (*svc_link_handler_t)(void *ctx, const s3w_v1_Envelope *req);

/** Handle Envelope bodies with this oneof tag (s3w_v1_Envelope_<name>_tag). ESP_ERR_NO_MEM: table full, INVALID_STATE: tag taken. */
esp_err_t svc_link_register(pb_size_t body_tag, svc_link_handler_t handler, void *ctx);

/** Answer `req` (from a handler). Fills reply->id/reply_to; set reply->status and the body. */
esp_err_t svc_link_reply(const s3w_v1_Envelope *req, s3w_v1_Envelope *reply);

/** Answer with an Ack body and the given Status (s3w_v1_StatusCode); msg may be NULL. */
esp_err_t svc_link_reply_status(const s3w_v1_Envelope *req, int32_t code, const char *msg);

/**
 * Send an event (id forced to 0) from any task. ESP_ERR_INVALID_STATE when the link is down,
 * ESP_ERR_NO_MEM when the queue or heap is full, ESP_ERR_INVALID_SIZE above 16 KB. Returns
 * once the message is queued; delivery is best effort (use svc_link_request for a reply).
 */
esp_err_t svc_link_send(s3w_v1_Envelope *env);

/** result is LINK_RPC_OK with the reply, or TIMEOUT / DISCONNECTED / FAILED with reply == NULL. */
typedef void (*svc_link_reply_cb_t)(void *ctx, link_rpc_result_t result, const s3w_v1_Envelope *reply);

/**
 * Send a request from any task: env->id is assigned here; the reply (or the failure after 3
 * retries, 5 s each) arrives in cb on the link task. Errors as svc_link_send().
 */
esp_err_t svc_link_request(s3w_v1_Envelope *env, svc_link_reply_cb_t cb, void *ctx);

/* --------------------------------------------------------------- transfers */

typedef struct {
    /**
     * TransferBegin arrived (the sink's kind): prepare storage. Set *resume_offset to what is
     * already stored for the same sha256 (0 = from the start). Return s3w_v1_StatusCode:
     * STATUS_OK, or NO_SPACE / BUSY / DENIED / INVALID to refuse.
     */
    int32_t (*open)(void *ctx, const s3w_v1_TransferBegin *begin, uint32_t *resume_offset);
    /** Next bytes, always sequential from the resume offset. false = storage failed (transfer ends). */
    bool (*write)(void *ctx, uint32_t offset, const uint8_t *data, size_t len);
    /** All bytes in: verify sha256 and commit. true = verified. */
    bool (*finish)(void *ctx);
    /** The transfer died (link lost, storage error, replaced). Keep the partial data for a resume if useful. May be NULL. */
    void (*abort)(void *ctx);
} svc_link_xfer_sink_t;

/** One sink per kind (s3w_v1_TransferKind). One transfer at a time across kinds (BUSY otherwise). */
esp_err_t svc_link_xfer_register(uint32_t kind, const svc_link_xfer_sink_t *sink, void *ctx);

typedef struct {
    uint32_t kind;     // s3w_v1_TransferKind
    char name[64];
    uint32_t size;
    uint8_t sha256[32];
    /** Read len bytes at offset (the link task: keep it quick). false = failed (upload ends). */
    bool (*read)(void *ctx, uint32_t offset, uint8_t *buf, size_t len);
    /** err: ESP_OK verified by the phone; ESP_ERR_TIMEOUT; ESP_ERR_INVALID_STATE link lost / busy; ESP_FAIL refused or hash mismatch (peer_status: s3w_v1_StatusCode). */
    void (*done)(void *ctx, esp_err_t err, int32_t peer_status);
    void *ctx;
} svc_link_upload_t;

/** Start a watch → phone transfer; one at a time. The descriptor is copied. */
esp_err_t svc_link_upload(const svc_link_upload_t *up);

/* ------------------------------------------------------------------ status */

typedef struct {
    bool up;
    uint16_t mtu;
    uint32_t rx_messages, tx_messages;
    uint32_t rx_dropped; // control messages lost to gaps / overruns / bad protobuf
    uint32_t rx_dups;
    uint32_t rpc_retries, rpc_timeouts;
    uint32_t queue_full; // frames or messages dropped because the link task was behind
    uint32_t bulk_rx_bytes, bulk_tx_bytes;
    bool xfer_in; // a phone → watch transfer is running
    bool xfer_out;
} svc_link_status_t;

void svc_link_get_status(svc_link_status_t *out);

#ifdef __cplusplus
}
#endif
