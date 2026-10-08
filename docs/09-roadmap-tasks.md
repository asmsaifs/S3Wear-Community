# 09 — Roadmap & task backlog

How to use this with an AI coding agent:
1. Start a fresh session per task (or per 2–3 small tasks). Paste the **prompt template** below with the task ID.
2. Review the plan the agent proposes before it writes code.
3. Run the acceptance checks yourself on hardware where marked 🔧.
4. Commit per task: `feat(fw/svc_power): P1-05 AXP2101 driver and battery events`.

### Prompt template
```
You are working in the S3Wear monorepo. Read CLAUDE.md, PLAN.md and <relevant doc(s)>.
Implement task <ID> from docs/09-roadmap-tasks.md exactly.
First: list the files you will create/modify and how you will meet each acceptance criterion. Wait for my OK.
Then implement, build (target + simulator/host tests as relevant), and report results with command output.
For criteria marked 🔧 give me step-by-step manual hardware checks and the console output I should expect.
Do not start other tasks. Do not change unrelated files.
```

Legend: 🔧 needs real hardware · 🖥 verifiable in simulator/host · ⏱ estimate in agent sessions (S ≈ 1, M ≈ 2–3, L ≈ 4+)

---

## Phase 0 — Foundation
| ID | Task | Acceptance criteria |
|---|---|---|
| P0-01 | Create monorepo skeleton per PLAN.md §3, `.gitignore`, `.editorconfig`, README, LICENSE (choose: Apache-2.0 for code, CC-BY for docs), `git init` | Tree matches; README links docs ⏱S |
| P0-02 | ESP-IDF project `firmware/` with `sdkconfig.defaults` (esp32s3, 240 MHz, octal PSRAM 80 MHz, 32 MB flash QIO 80 MHz, USB-Serial-JTAG console, PM + tickless idle, FreeRTOS 1000 Hz, C++20) and `partitions.csv` from 02-architecture §8; `idf_component.yml` with pinned LVGL 9.3.x, esp_lcd_touch_ft5x06, esp_codec_dev, joltwallet/littlefs, nanopb, esp-sr, wasm-micro-runtime (pinned versions) | `idf.py build` succeeds; app prints "S3Wear boot" 🔧 flashes and logs over USB ⏱M |
| P0-03 | Host test project `firmware/host_test` (CMake + GoogleTest via FetchContent) with one sample test | `ctest` passes 🖥 ⏱S |
| P0-04 | Simulator project `firmware/simulator` (CMake, SDL2, LVGL) showing a 410×502 window with "Hello"; headless `--screenshot` flag | Runs on macOS/Linux; PNG produced 🖥 ⏱M |
| P0-05 | Android project skeleton: version catalog, convention plugins, `:app`, `:core:*` empty modules, Hilt, Compose M3 theme, ktlint/detekt config | `./gradlew build` green ⏱M |
| P0-06 | `protocol/` with `envelope.proto` (Envelope + Hello/HelloAck/TimeSync/Ack/Status), `tools/gen_proto.sh` generating nanopb C into `firmware/components/proto` and Wire Kotlin into `:core:protocol` | Both build; one golden vector round-trips in C and Kotlin tests ⏱M |
| P0-07 | CI workflows `firmware.yml`, `android.yml`, `protocol.yml` | Green on push ⏱M |

## Phase 1 — Board bring-up (every chip proven)
Hardware check procedure with expected console output: [bringup-phase1.md](bringup-phase1.md).

| ID | Task | Acceptance criteria |
|---|---|---|
| P1-01 | `bsp_s3w`: `bsp_s3w_pins.h` (from 01-hardware §2), `bsp_init_early()` (PA low, I2C master bus 400 kHz on 14/15, buttons GPIO0/GPIO10 with debounce), `esp_console` with `i2c scan` | 🔧 `i2c scan` lists all 6 devices; update addresses table in 01-hardware ⏱S |
| P1-02 | `drv_co5300` (esp_lcd panel driver: init sequence and gap from Waveshare demo, brightness 0x51, sleep in/out, display on/off) + LVGL port in `ui_framework/lvgl_port` (partial mode, 2 DMA buffers, rounder for even/odd alignment, TE-synced flush, `lv_lock`) | 🔧 colour bars + LVGL demo widgets render without tearing or offset; console `lcd bright 0..255`; measured full-screen fps logged ⏱L |
| P1-03 | `drv_ft3168` via `esp_lcd_touch` (INT GPIO38, RST GPIO9), LVGL indev, gestures (swipe dirs, long-press), monitor-mode enter/exit | 🔧 touch draw test app; swipe events logged; tap wakes from monitor mode ⏱M |
| P1-04 | `drv_pcf85063`: read/write time, oscillator-stop flag, alarm + INT (GPIO39), offset register; sync to system time at boot | 🔧 time survives reset; console `rtc get/set/alarm +60` fires interrupt; 🖥 host tests for BCD conversions ⏱S |
| P1-05 | `drv_axp2101` (own driver or XPowersLib wrapped): rails, charger (200 mA CC), battery %, mV, VBUS, charging status, die temp, power-key IRQ (short/long), shutdown; record rail mapping in 01-hardware §4 | 🔧 console `pmu` prints all values; plugging USB emits event; long-press PWR 6 s hard-off works ⏱M |
| P1-06 | `drv_qmi8658`: accel/gyro config, FIFO with watermark IRQ on GPIO21, wake-on-motion, tap detection, built-in pedometer enable/read, self-test, low-power mode | 🔧 console `imu stream` shows ~1 g on Z flat; `imu steps` counts while walking; WoM IRQ fires ⏱M |
| P1-07 | `drv_audio`: esp_codec_dev for ES8311 (out) + ES7210 (in, 2 mics) on full-duplex I2S (MCLK 16, BCLK 41, WS 45, DOUT 40, DIN 42), PA_CTRL 46, volume, mute, power-down | 🔧 `audio tone 1000 500` audible; `audio rec 3` then `audio play` plays back; loopback test detects tone ⏱M |
| P1-08 | TF card via SDSPI (1/2/3/17) FATFS mount at `/sd`, hot-plug tolerant; LittleFS mount `/flash` | 🔧 `sd info`, `fs ls /flash`; boot OK without card ⏱S |
| P1-09 | Factory test screen (LVGL) covering all of the above + BLE advertise + Wi-Fi scan, JSON result over console | 🔧 every item PASS on the dev board ⏱M |

## Phase 2 — Core OS
| ID | Task | Acceptance criteria |
|---|---|---|
| P2-01 | `sys_core`: event bus (esp_event wrapper + UI mailbox), logging config, task/lock wrappers, `svc_worker` job queue | 🖥 host tests for mailbox ordering; no LVGL access off-thread (assert in debug) ⏱M |
| P2-02 | Settings service: schema header, typed get/set, NVS backend, change events, defaults, factory reset | 🖥 host tests with fake NVS ⏱S |
| P2-03 | `hal/` interfaces + `hal_sim` implementations; move drivers behind HAL | Simulator boots the same UI code; target build unchanged 🖥 ⏱M |
| P2-04 | Display performance pass: compare partial vs direct PSRAM double buffer, QSPI 40 vs 80 MHz; pick and document | 🔧 report fps for list scroll and full-screen swipe; ≥ 30 fps scroll ⏱M |
| P2-05 | `ui_framework`: screen lifecycle, nav stack, home/back, edge-swipe back, overlays (toast/banner/full-screen alert), theme tokens, fonts pipeline (`tools/fonts`), standard widgets from 04-ui-ux §5 | 🖥 widget gallery screen + snapshot tests ⏱L |
| P2-06 | `svc_power` state machine (ACTIVE/DIM/AOD/SLEEP/SAVER/WATCH-ONLY/OFF), screen timeout, wake sources, PM locks, battery events, power menu | 🔧 screen off after timeout, light sleep entered (pm stats), wakes on tap/button; idle current measured and logged ⏱L |
| P2-07 | `svc_input`: buttons (short/long/double/triple), mapping to actions, palm-cover detection | 🔧 BOOT = back, PWR = home/off; triple-press event ⏱S |
| P2-08 | Diagnostics: console commands from 02-architecture §11, coredump to flash, metrics ring | 🔧 forced panic → coredump retrievable via console ⏱M |

## Phase 3 — Watch essentials
| ID | Task | Acceptance criteria |
|---|---|---|
| P3-01 | Time service: TZ, 12/24 h, drift correction, "time unknown" UI state | 🖥 host tests for DST transitions ⏱S |
| P3-02 | Watch face engine: native face API + complication registry/bindings + 4 native faces + AOD variants | 🖥 snapshots; AOD pixel-count test < 10 % ⏱L |
| P3-03 | Declarative face loader (`face.json` parser + renderer) with 2 sample faces | 🖥 parser tests incl. malformed input; snapshots ⏱M |
| P3-04 | Face picker + customize | 🖥 ⏱M |
| P3-05 | Raise-to-wake + tap-to-wake + AOD mode in `svc_power`/`svc_sensors`, burn-in pixel shift | 🔧 raise wakes < 150 ms; false wakes counted over 1 h walk ⏱L |
| P3-06 | Quick settings panel, notifications placeholder, tiles carousel, launcher (list + grid) | 🖥 navigation scenario snapshots ⏱M |
| P3-07 | Alarms (scheduler, RTC alarm, ring screen, snooze, flip-to-snooze), timers, stopwatch, world clock | 🖥 scheduler tests; 🔧 alarm fires from SLEEP and WATCH-ONLY ⏱L |
| P3-08 | DND / sleep / theater modes + schedules | 🖥 ⏱S |
| P3-09 | Battery screen, charging screen, low-battery flows, saver & watch-only modes | 🔧 watch-only mode shows time and wakes on PWR ⏱M |
| P3-10 | On-watch Settings app (tree from 03-features F18) | 🖥 ⏱L |
| P3-11 | System sounds service (`svc_audio` mixer, categories, volumes, silent) | 🔧 ⏱M |

## Phase 4 — Basic companion
Pairing, time sync and the watch battery on the phone; nothing else. Spec: [07-android-app.md](07-android-app.md).

| ID | Task | Acceptance criteria |
|---|---|---|
| P4-01 | Firmware `svc_ble`: NimBLE GAP/GATT, advertising, LESC numeric comparison with pairing screen, bond storage, single-companion policy, BAS + DIS | 🔧 nRF Connect sees services; pairing code shown ⏱M |
| P4-02 | Firmware `svc_link`: framing, reassembly, envelope dispatch, request/reply with retries, bulk engine (windowed, resume) | 🖥 host tests with loss injection + golden vectors ⏱L |
| P4-03 | Android `:core:ble` + `:core:protocol`: WatchConnection state machine, MTU/PHY, framing, bulk engine; `:core:testing` FakeWatch | Unit + integration tests green ⏱L |
| P4-04 | Android onboarding with CDM association + pairing + permissions + profile | Manual: fresh install → paired in < 2 min ⏱M |
| P4-05 | `:service:link` foreground service, reconnect, router, outbox | 🔧 survives app swipe-away and phone reboot (reconnects < 30 s) ⏱M |
| P4-06 | Handshake + TimeSync + DeviceStatus (battery on phone dashboard) | 🔧 watch time set from phone; dashboard shows battery ⏱S |

---

## Suggested order and milestones
- **M1 "It's a watch"** (P0–P3): standalone watch with faces, alarms, raise-to-wake, battery handling. Done.
- **M2 "It talks to a phone"** (P4 basic): pairing, time sync, watch battery on the phone.

## Risks & mitigations
| Risk | Impact | Mitigation |
|---|---|---|
| Battery life below target (AMOLED + ESP32-S3 idle current) | High | Measure early (P2-06), keep light sleep the default, slow BLE intervals, AOD off by default, rail gating |
| Display throughput limits animation smoothness | Medium | Partial updates, fewer full-screen animations, 80 MHz QSPI if stable |
| Android OEM background killing | High | CDM association, foreground service, vendor-specific guidance screen, reconnect on `onDeviceAppeared` |
| Internal RAM pressure (BLE + LVGL) | Medium | PSRAM for big buffers, free-heap monitor |
| No haptics hurts alarm noticeability | Medium | Distinct sounds, optional external motor (GPIO43) |
| Strapping/eFuse mistakes brick a unit | High | Release security only via script on spare units, documented warnings |
