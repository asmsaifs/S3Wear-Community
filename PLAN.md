# S3Wear Community — Plan

Open-source smartwatch OS ("S3Wear OS", Community edition) for the **Waveshare ESP32-S3-Touch-AMOLED-2.06**: a standalone watch with faces, alarms, timers, battery management and settings, plus a basic Android companion (planned).

This file is the entry point. Read it first, then the document for the phase you are working on. Every document is written so an AI coding agent can implement it task by task. Each task has an ID, the files it touches, and acceptance criteria.

| # | Document | Purpose |
|---|----------|---------|
| 0 | [PLAN.md](PLAN.md) | Vision, scope, decisions, repo layout, roadmap |
| 1 | [CLAUDE.md](CLAUDE.md) | Rules for AI agents working in this repo (read before every session) |
| 2 | [docs/01-hardware.md](docs/01-hardware.md) | Hardware inventory, pin map, I2C map, gotchas, what the board cannot do |
| 3 | [docs/02-firmware-architecture.md](docs/02-firmware-architecture.md) | Layers, tasks, memory, power, storage, security |
| 4 | [docs/03-firmware-features.md](docs/03-firmware-features.md) | Spec for every user-facing watch feature |
| 5 | [docs/04-ui-ux.md](docs/04-ui-ux.md) | Design system and navigation model for a 410×502 AMOLED |
| 6 | [docs/06-ble-protocol.md](docs/06-ble-protocol.md) | Watch ↔ phone protocol for the basic companion |
| 6 | [docs/07-android-app.md](docs/07-android-app.md) | Basic Android companion (planned) |
| 7 | [docs/08-quality-ci-release.md](docs/08-quality-ci-release.md) | Testing, CI/CD, release, manufacturing/provisioning |
| 8 | [docs/09-roadmap-tasks.md](docs/09-roadmap-tasks.md) | Phase-by-phase task backlog with acceptance criteria and agent prompts |

---

## 1. Vision

A reliable standalone watch on open hardware: always correct time, a watch face that is on when you raise your wrist, alarms that never miss, honest battery handling, and a clean, documented codebase you can learn from and build on. It should run for **2+ days** on the 400 mAh cell under normal use (screen-on ~60 min/day, no always-on display).

### Pillars
1. **Reliable watch first.** Time, alarms, battery and wake-on-raise must never break. No feature can block the UI thread or drain the battery.
2. **Use the board well.** AMOLED, touch, 6-axis IMU, RTC, PMU/fuel gauge, speaker, TF card, two buttons.
3. **Testable.** A desktop simulator with snapshot tests for every screen, host tests for pure logic, CI.

### Out of scope (hardware does not support it)
| Feature | Why | Mitigation |
|---|---|---|
| Heart rate / SpO2 / ECG | No optical sensor | Optional I2C add-on (MAX30102) |
| Built-in GPS | No GNSS | None |
| Compass / altimeter | No magnetometer or barometer | Optional I2C add-on (QMC5883L / BMP390) behind a feature flag |
| Phone call audio on the watch | ESP32-S3 has BLE only, no Classic Bluetooth (no HFP/A2DP) | None |
| Bluetooth headphones | Same: no Classic BT | None |
| NFC payments | No NFC | None |
| Vibration | No motor on the board | Sound and screen cues; optional motor on free GPIO 43/44 via a MOSFET (feature flag `HAPTICS_EXT`) |

---

## 2. Key technical decisions

| Area | Decision | Reason |
|---|---|---|
| Firmware framework | **ESP-IDF v5.5.x** (pin the exact version in CI; move to v6 only as a planned task) | Production support: OTA, secure boot, flash encryption, power management, NimBLE, coredumps |
| Language | C for drivers, **C++20** (no exceptions, no RTTI) for services/UI | Matches ESP-IDF; RAII for locks and handles |
| UI | **LVGL 9.3+** | Vendor examples use 9.3; mature; has a PC simulator |
| BLE stack | **NimBLE** | Smaller RAM than Bluedroid; BLE only, which is all we need |
| Serialization | **Protocol Buffers**: nanopb on the watch, Wire/protobuf-lite on Android | Schema-first, versionable, same `.proto` files for both sides |
| Filesystem | LittleFS on internal flash (`/flash`), FATFS on TF card (`/sd`) | Power-loss safe internal FS; PC-readable SD |
| Settings | NVS (encrypted in release) behind a typed settings service | Atomic, wear-levelled |
| Android | **Kotlin, Jetpack Compose, Material 3**, Hilt, Coroutines/Flow, Room, DataStore, WorkManager, Nordic Android-BLE-Library, CompanionDeviceManager | Modern standard stack, background-friendly BLE |
| Android SDK | `minSdk 29`, `targetSdk` = latest stable required by Play | Simplifies BLE permissions |
| Monorepo | One repo for firmware, protocol, Android | Protocol changes land atomically on both sides |

---

## 3. Repository layout

```
S3Wear/
├── PLAN.md                    # this file
├── CLAUDE.md                  # agent rules
├── docs/                      # all specs
├── protocol/
│   ├── proto/                 # *.proto — single source of truth for watch<->phone messages
│   ├── testvectors/           # golden binary frames used by C and Kotlin tests
│   └── README.md
├── firmware/                  # ESP-IDF project
│   ├── CMakeLists.txt
│   ├── partitions.csv
│   ├── sdkconfig.defaults
│   ├── sdkconfig.defaults.release
│   ├── main/                  # app_main: boot sequence only
│   ├── components/
│   │   ├── bsp_s3w/           # pins, bus init, power rails — the only place pin numbers live
│   │   ├── drv_co5300/        # AMOLED panel (esp_lcd)
│   │   ├── drv_ft3168/        # touch (esp_lcd_touch)
│   │   ├── drv_qmi8658/       # IMU incl. FIFO, WoM, pedometer, tap
│   │   ├── drv_pcf85063/      # RTC
│   │   ├── drv_axp2101/       # PMU / fuel gauge / charger / power key
│   │   ├── drv_audio/         # ES8311 + ES7210 via esp_codec_dev, NS4150B PA
│   │   ├── s3w_hal/           # HAL interfaces + watch impl (sim impl: simulator/hal_sim); "hal" is taken by ESP-IDF
│   │   ├── sys_core/          # event bus, logging, settings, time, error, util
│   │   ├── svc_*/             # services (power, input, sensors, alarm, modes, audio, time, settings, diag)
│   │   ├── ui_framework/      # navigation, screens, theme, widgets, gestures
│   │   ├── ui_apps/           # native system apps (settings, clock apps, battery, shell)
│   │   ├── watchfaces/        # native faces + declarative face engine
│   │   ├── nanopb/            # vendored nanopb runtime, pinned (see its README.md)
│   │   └── proto/             # nanopb generated code (do not edit by hand)
│   ├── simulator/             # desktop build (CMake + SDL2) with mock HAL
│   ├── test/                  # on-target Unity tests (pytest-embedded)
│   └── host_test/             # host GoogleTest for pure-logic components
├── android/                   # Gradle project (basic companion, planned)
├── tools/                     # font converter, protocol codegen
└── .github/workflows/
```

---

## 4. Feature matrix

| Feature | Hardware used | Status |
|---|---|---|
| Watch faces (4 native + declarative `face.json`), complications, picker, customize | AMOLED, RTC | Done (Phase 3) |
| Always-on display (AOD), burn-in shift | AMOLED low-power mode | Done |
| Raise-to-wake, tap-to-wake, palm cover | IMU, touch | Done |
| Quick settings, tiles, launcher | all | Done |
| Alarms (wake from deep sleep), timers, stopwatch, world clock | RTC, speaker | Done |
| DND, sleep mode, theater mode, schedules | — | Done |
| Battery app, charging screen, low-battery flows, saver, watch-only mode | AXP2101 | Done |
| On-watch Settings app | — | Done |
| System sounds | speaker | Done |
| Basic companion: pairing, time sync, phone shows watch battery | BLE | Planned (Phase 4 basic) |

---

## 5. Roadmap (summary)

Detailed tasks: [docs/09-roadmap-tasks.md](docs/09-roadmap-tasks.md).

| Phase | Name | Outcome / exit criteria |
|---|---|---|
| 0 | Foundation | Monorepo, toolchains pinned, CI builds firmware + Android skeleton + simulator |
| 1 | Board bring-up | Every chip on the board works from a factory-test screen; console commands for each |
| 2 | Core OS | Event bus, settings, logging, power manager, LVGL + navigation framework, simulator parity |
| 3 | Watch essentials | Faces, AOD, raise-to-wake, quick settings, alarms/timers/stopwatch, DND, battery UI, settings, sounds |
| 4 | Basic companion | Pairing, time sync, watch battery on the phone |

---

## 6. Definition of done (every task)

1. Builds for target (`idf.py build`) with zero new warnings, and builds in the simulator if UI is touched.
2. Unit tests added or updated and passing (host tests for logic, Kotlin unit tests for Android).
3. Acceptance criteria in the task are demonstrated: either an automated test, a console command output, or a simulator screenshot. Hardware-only criteria are written as a manual test step in the PR.
4. No blocking calls on the LVGL thread; no allocation > 4 KB from internal RAM without justification.
5. Docs in `docs/` updated if behaviour, protocol or API changed. Protocol changes bump the protocol minor version and add a test vector.
6. Power impact noted for anything that wakes the CPU periodically.
