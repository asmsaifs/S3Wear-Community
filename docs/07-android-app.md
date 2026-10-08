# 07 — Basic Android companion (planned)

The Community companion does three things: pair with the watch, keep its time and time zone correct, and show the watch battery. Tasks: Phase 4 in [09-roadmap-tasks.md](09-roadmap-tasks.md). The skeleton in `android/` (P0-05) is where it grows.

## Scope
| Feature | Watch side | Phone side |
|---|---|---|
| Pairing | `svc_ble`: advertising, LE Secure Connections with numeric comparison (6-digit code on both screens), one bonded phone | CompanionDeviceManager association, system pairing dialog |
| Time sync | Sets RTC, system time and POSIX time zone on `TimeSync` | Sends `TimeSync` on connect, on time zone change and every 6 h |
| Watch battery | `DeviceStatus` on connect and on every 1 % change | Dashboard card: battery %, charging, last seen |

Not in scope: notifications, media, weather, health, app store, firmware updates.

## Stack
Kotlin, Jetpack Compose, Material 3, Hilt, Coroutines/Flow, DataStore, Nordic Android-BLE-Library, CompanionDeviceManager. `minSdk 29`.

## Modules
`:app` → `:feature:*` → `:core:*`. Features depend on `core:*`, never on other features.

| Module | Contents |
|---|---|
| `:core:protocol` | Framing, protobuf messages generated from `protocol/proto`, golden vector tests |
| `:core:ble` | `WatchConnection` (the only code that touches `BluetoothGatt`) |
| `:core:designsystem` | Theme, colours, type |
| `:feature:onboarding` | Association, pairing, permissions |
| `:feature:dashboard` | Connection state, watch battery |

## Permissions
| Permission | Why |
|---|---|
| `BLUETOOTH_CONNECT` | Talk to the paired watch |
| `BLUETOOTH_SCAN` (`neverForLocation`) | Find the watch during pairing |
| `FOREGROUND_SERVICE` + `FOREGROUND_SERVICE_CONNECTED_DEVICE` | Keep the link up for time sync |
| `POST_NOTIFICATIONS` | The foreground service notification (Android 13+) |
