# protocol

Watch ↔ phone protocol. Spec: [docs/06-ble-protocol.md](../docs/06-ble-protocol.md).

- `proto/` — `*.proto` files, the single source of truth. Never reuse or renumber a field; add fields as optional.
- `testvectors/` — golden binary frames; every message has one, used by both the C and Kotlin tests.

- `proto/*.options` — nanopb buffer sizes for strings and repeated fields (watch limits).

`tools/gen_proto.sh` regenerates everything (`--check` verifies committed output is current):

| Output | Tool | Committed |
|---|---|---|
| `firmware/components/proto/` (C) | nanopb generator 0.4.9.2 (same tag as the vendored runtime) | yes, never edit by hand |
| `testvectors/*.bin` from `*.json` | Google protobuf (Python) | yes |
| Android `:core:protocol` (Kotlin) | Wire Gradle plugin, at build time from `proto/` | no |

The script installs its Python tools (pinned) into `build/protogen/`; it needs only `python3`, `curl` and a JDK.

Adding a message: edit the `.proto` (+ `.options` limits), add `testvectors/envelope_<body>.json`,
run `tools/gen_proto.sh`, add a TEST in `firmware/host_test/test_proto_vectors.cpp`
(the Kotlin test picks it up automatically; register new message types in its `messages` map).
