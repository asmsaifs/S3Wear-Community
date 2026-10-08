# 06 — Watch ↔ phone protocol (S3W Link)

Protocol version: **1.0**. Source of truth: `protocol/proto/*.proto`. This document explains transport, framing, flows and the messages the basic companion uses (pairing, time sync, watch status). Field numbers not listed here are reserved; never reuse them.

## 1. GATT layout

| Service / characteristic | UUID | Properties | Use |
|---|---|---|---|
| **S3W Link service** | `7a3e0001-5a1b-4c8e-9f2d-3b6c1e0d4a77` | — | |
| RX (phone → watch) | `7a3e0002-5a1b-4c8e-9f2d-3b6c1e0d4a77` | Write, Write Without Response | control frames |
| TX (watch → phone) | `7a3e0003-5a1b-4c8e-9f2d-3b6c1e0d4a77` | Notify | control frames |
| Battery Service | `0x180F` | Read, Notify | standard battery level |
| Device Information | `0x180A` | Read | manufacturer, model "S3Wear-206", fw rev, hw rev, serial |
| Current Time Service (client role, optional) | `0x1805` | — | fallback time source |

All S3W characteristics require an **encrypted, bonded** link (LE Secure Connections). Advertising: name `S3Wear-XXXX` (last 2 bytes of MAC), service UUID in scan response, manufacturer data `{company 0xFFFF (dev), model id, pairing state flag}`. Phone uses `CompanionDeviceManager` filtering on the service UUID.

Link parameters: request MTU 517 (use negotiated), Data Length Extension 251, 2M PHY when supported. Connection interval: slow (≥ 400 ms, latency 4) when idle.

## 2. Framing (RX/TX)

Each GATT write/notify carries one **frame**:

```
 0      1      2      3      4 ...
+------+------+------+------+---------------------+
| hdr  | seq         | channel | payload            |
+------+------+------+------+---------------------+
hdr   : bits 7..6 = frag (00 single, 01 first, 10 middle, 11 last)
        bits 5..0 = protocol major version (1)
seq   : uint16 little-endian, per-direction, per-channel, wraps
chan  : 0 = control (1 is reserved)
```
- Control channel: fragments reassemble into one serialized `Envelope` protobuf (max 16 KB). First fragment payload starts with `uint16 total_len`.
- Lost/duplicate detection by `seq`; on a gap in control reassembly the receiver discards the message and the sender's request times out and retries.

## 3. Envelope & RPC semantics

```proto
syntax = "proto3";
package s3w.v1;

message Envelope {
  uint32 id = 1;          // sender-assigned, non-zero for requests
  uint32 reply_to = 2;    // id of the request this answers (0 = not a reply)
  Status status = 3;      // for replies
  oneof body {
    Hello hello = 10;
    HelloAck hello_ack = 11;
    TimeSync time_sync = 12;
    Ack ack = 13;
    DeviceStatus device_status = 14;
    // 15 and up: reserved
  }
}

message Status { int32 code = 1; string message = 2; } // 0 OK, 1 UNSUPPORTED, 2 INVALID, 3 BUSY, 4 NO_SPACE, 5 DENIED, 6 INTERNAL, 7 LOW_BATTERY
```
- Requests expect a reply within 5 s or are retried up to 3 times with the same `id` (receivers dedupe on id).
- Events (`id = 0`) are fire-and-forget.
- Unknown `oneof` fields are ignored (forward compatibility). `Hello` carries capabilities so new features are only used when both sides support them.

## 4. Session start

```
Phone                                   Watch
  | connect, bond (LESC numeric comparison, first time only)
  | discover, MTU 517, enable notifications
  |-- Hello{proto 1.0, app_ver, caps[], phone_model, locale, tz} -->|
  |<-- HelloAck{proto 1.0, fw_ver, hw_rev, serial, caps[], battery, storage, api_level} --|
  |-- TimeSync{unix_ms, tz_posix, tz_name, is_24h} -------------->|
  |<-- DeviceStatus{battery, charging} ----------------------------|
```
Capabilities are strings; the basic companion defines none yet.

## 5. Versioning rules
- Major version in frame header; mismatch → watch replies `HelloAck{status=UNSUPPORTED}`, phone asks the user to update the firmware or the app.
- Minor changes = new optional fields/messages + new capability string.
- Golden vectors: `protocol/testvectors/<message>_<case>.bin` + `.json` (human-readable equivalent). C tests (host) and Kotlin tests decode each `.bin` and compare with `.json`, then re-encode and compare bytes.
  - `<message>` is the snake_case message name (longest match: `hello_ack_x` is a `HelloAck`); control-channel vectors are `envelope_<body>[_<case>]`.
  - The `.json` (proto3 JSON: camelCase names, int64 as string) is written by hand; `tools/gen_proto.sh` encodes the `.bin` with Google's Python protobuf, independent of nanopb and Wire. `tools/gen_proto.sh --check` fails on stale output.
  - C: `firmware/host_test/test_proto_vectors.cpp` (one TEST per vector; a guard test fails if a vector has none). Kotlin: `android/core/protocol` `GoldenVectorsTest` (runs every vector automatically).
- Field order: declare fields in ascending field-number order so nanopb, Wire and protobuf emit identical bytes.
- String and repeated-field limits are fixed-size buffers on the watch (`protocol/proto/*.options`, mirrored as `≤ N bytes` comments in the `.proto`). Senders must stay within them; the watch rejects a message that exceeds one.

## 6. Security
- Bonding with LESC numeric comparison; watch displays the 6-digit code, phone shows the same; user confirms on both.
- Only one bonded companion phone (re-pair requires confirm on watch).
- Watch rejects all S3W writes from unbonded / unencrypted connections.
