# 02 — Firmware architecture

## 1. Layers

```
┌───────────────────────────────┬──────────────────────────────────────┐
│ ui_apps (native system apps)  │ watchfaces (native + declarative)    │
├───────────────────────────────┴──────────────────────────────────────┤
│ ui_framework: navigation stack, screen lifecycle, gestures, theme,   │
│               widgets, toasts/overlays, LVGL port                    │
├──────────────────────────────────────────────────────────────────────┤
│ Services (svc_*): power, input, sensors, alarm, modes, audio, time,  │
│ settings, diag                                                       │
├──────────────────────────────────────────────────────────────────────┤
│ sys_core: event bus, settings store, logging, time, task/lock wrappers│
├──────────────────────────────────────────────────────────────────────┤
│ hal: display, touch, imu, rtc, pmu, audio, buttons, storage, haptics │
├──────────────────────────────────────────────────────────────────────┤
│ bsp_s3w + drivers (co5300, ft3168, qmi8658, pcf85063, axp2101,       │
│ es8311/es7210 via esp_codec_dev)  |  simulator: mock HAL over SDL     │
└──────────────────────────────────────────────────────────────────────┘
```

Rules: a layer only calls the layer below. Upward communication is via the event bus. The simulator replaces `bsp_s3w + drivers` with `hal_sim` so the UI, watch faces and services logic run on a PC.

HAL (P2-03, `components/s3w_hal` — the component cannot be called `hal`, ESP-IDF owns that name):
- Interfaces in `s3w_hal/include/hal_*.h` use only standard C types and `esp_err_t`: `hal_display` (size, brightness), `hal_input` (touch point, BACK/POWER buttons), `hal_rtc` (UTC get/set with a "lost power" flag, one alarm), `hal_pmu` (battery %, mV, charging, VBUS; charger/power-key events; power off), `hal_storage` (SD mount/present), `hal_power` (light-sleep wake inputs, deep sleep; P2-06). P2-06 also added panel power (`hal_display_set_power`) and touch low-power mode + touch-down callback (`hal_input`); P2-07 added the touch contact callback (point count, bounding box, contact area per sample, for palm detection). P3-05 added `hal_imu` (accelerometer in the watch frame, wake-on-motion, IMU interrupt) and the `HAL_WAKE_MOTION` wake input. P3-07 added a timer to `hal_power_deep_sleep(timer_us)` (alarms in WATCH-ONLY); P3-09 added `keep_display` (the panel stays on through deep sleep). Audio and haptics interfaces are added with the services that define their needs (`svc_audio` P3-11 uses `drv_audio` directly, like the console; a `hal_audio` comes with the first host-side consumer).
- Watch implementation `s3w_hal/s3w/` is a thin layer over `bsp_s3w`; the simulator links `simulator/hal_sim/` instead (SDL mouse/keys, host clock, fake battery). Selection is at link time, no vtables.
- Who may still call `bsp_s3w`/`drv_*` directly: `app_main` (board bring-up order), `svc_diag` and `factory_test` (hardware diagnostics, watch-only), and the watch-only LVGL port (`ui_framework/lvgl_port`, needs the esp_lcd panel). Everything else — services, `ui_framework/ui`, apps, faces — uses the HAL.
- Portable UI code lives in `ui_framework/ui/` (LVGL + HAL only): boot screen and HAL pointer indev (`ui_root.h`), theme and fonts (`ui_theme.h`), navigation (`ui_nav.h`), overlays (`ui_overlay.h`), widgets (`ui_widgets.h`), built-in screens (`ui_screens.h`); docs/04 §2–5. Both builds compile the list in `ui_framework/ui/sources.cmake`. Host builds get `esp_err.h` from `firmware/host_stubs/`.

## 2. Boot sequence (`main/app_main.c`)

1. `bsp_init_early()`: PA_CTRL low, I2C bus, AXP2101 (rails on, charger config, read reset reason, VBUS).
2. If battery < cut-off and no VBUS → show empty-battery icon 2 s → power off via AXP2101.
3. `sys_core_init()`: event bus, logging, coredump check (if a crash dump exists, mark for upload); then `svc_worker_start()` and `svc_settings_init()` (NVS + settings).
4. Time: `bsp_rtc_start()` then `svc_time_start()`: PCF85063 → system time, zone from settings, drift trim re-applied. If the RTC is invalid (oscillator stop flag) → "time unknown" (§5).
5. Display + touch + LVGL port; show boot logo (from flash, < 300 ms after reset).
6. Mount LittleFS (`/flash`); mount SD (`/sd`) in background, non-fatal.
7. Start services in dependency order (table below). Each `svc_x_start()` must return in < 50 ms; slow init runs in the service's own task.
8. Navigate to the watch face.

Target: reset → watch face visible ≤ 1.5 s.

## 3. Tasks, cores, priorities

| Task | Core | Prio | Stack | Notes |
|---|---|---|---|---|
| NimBLE host | 0 | 21 (IDF default) | 4 KB | factory test advertising only |
| `svc_sensors` (raise to wake; later IMU FIFO, step/sleep algos) | 0 | 12 | 3 KB (PSRAM) | wakes on the IMU interrupt; 50 Hz reads only during a raise window |
| `svc_audio` | 0 | 15 | 6 KB | I2S DMA, codec, mixer |
| `svc_worker` (generic job queue: flash writes, file ops) | 0 | 5 | 6 KB | prevents ad-hoc tasks |
| `ui` (LVGL timer handler) | 1 | 8 | 12 KB | owns all LVGL objects |
| `svc_power` | 0 | 18 | 4 KB | screen states, PM locks, wake inputs, battery (P2-06, §7) |
| `svc_alarm` | 0 | 13 | 4 KB | alarms, timers, ringing (P3-07, §7); idle on its queue except while ringing |
| `s3w_evt` (event bus loop) | 0 | 11 | 4 KB | sys_core bus, see §4 |
| `swdraw` (LVGL SW renderer) | any | 3 | 8 KB | created by LVGL when `LV_USE_OS` is FreeRTOS; renders for the `ui` task |
| `touch` (bsp_s3w) | 0 | 9 | 3 KB | FT3168 reader, wakes on TP_INT |

The constants live in `sys_core/include/s3w_task.h`; create tasks with `s3w_task_create()` (BSP tasks below sys_core are the exception).

All services that do not need a task (settings, input, time, modes) run in the caller's context or on the `esp_timer` task with short callbacks. Alarm scheduling is `esp_timer` callbacks too; `svc_alarm` has a task only to write NVS, program the RTC and feed the speaker while ringing.

## 4. Event bus

`sys_core/event_bus.h` — thin wrapper over `esp_event` with a dedicated loop task (core 0, prio 11) plus a UI-side mailbox.

```c
// Declare in svc_<name>_events.h
ESP_EVENT_DECLARE_BASE(SVC_POWER_EVENT);
typedef enum { SVC_POWER_EVT_STATE, SVC_POWER_EVT_BATTERY, SVC_POWER_EVT_BATTERY_LOW } svc_power_event_t;
typedef struct { int8_t percent; uint16_t mv; bool charging; bool vbus; } svc_power_battery_t;

// Publish (any task, not ISR)
s3w_event_post(SVC_POWER_EVENT, SVC_POWER_EVT_BATTERY, &evt, sizeof evt);

// Subscribe from UI: callback runs in UI task (safe to touch LVGL)
s3w_ui_subscribe(SVC_POWER_EVENT, SVC_POWER_EVT_BATTERY, on_battery, ctx);
```

Payloads are copied (≤ 128 bytes). Large data goes through a shared store (e.g. `notify_store_get(id)`), never the bus.

Implemented in P2-01 (`components/sys_core`):
- `s3w_event_post/subscribe/unsubscribe` on a dedicated loop `s3w_evt` (queue 32; post waits ≤ 10 ms, then `ESP_ERR_TIMEOUT`). Callbacks get `(ctx, base, id, data, len)`.
- `s3w_ui_subscribe` delivers through the **UI mailbox** (32 slots in PSRAM, FIFO, payload copied). `s3w_ui_post(fn, ctx)` / `s3w_ui_post_data()` queue arbitrary work for the UI task, which drains the mailbox under `lv_lock()` before every `lv_timer_handler()` and is woken by task notification index 1 (index 0 belongs to LVGL's render-thread sync).
- IDF (Wi-Fi/netif) and BSP driver events (`BSP_PMU_EVENT`) stay on the **default** loop; the owning service (e.g. `svc_power`) subscribes there and republishes on the bus.
- Debug builds (`CONFIG_S3W_LVGL_THREAD_CHECK`) abort with a backtrace when an LVGL object is invalidated or LVGL memory is allocated by a task that does not hold `lv_lock()` (console: `ui bad`). `ui test` checks mailbox and bus→UI ordering on the device.
- `svc_worker_submit(fn, ctx)` / `svc_worker_submit_copy()` run slow jobs (flash/file/SD) in order on the `svc_worker` task.

## 5. Data model / state stores
Services own their state and expose read-only getters plus change events:
- `time`: system time, timezone (POSIX TZ string from phone), 12/24 h, "time valid" flag.

### Time (P3-01, `components/svc_time`)
- `svc_time_start()` (boot step 4) loads the drift state (NVS `s3w_time/drift`), re-applies the PCF85063 offset register (lost with the RTC's power), sets system time from the RTC and applies the zone. An RTC that lost power leaves the time **unknown** (`svc_time_is_valid()` false): every clock shows `--:--` and the home screen says "Time not set / Connect your phone" (`ui_clock_set_valid()`, `ui_clock_bind_unknown()`), until a sync or a manual set.
- `svc_time_set_utc_ms(ms, source)` (phone `TimeSync` in P4, SNTP in P9, console now) sets system time at once and rewrites the RTC on the next whole second (one-shot `esp_timer`). Not from the UI task (one I2C read).
- **Zone**: POSIX TZ string in the `TIMEZONE` setting, validated by `tz_posix.c` (POSIX.1-2024 grammar plus the RFC 8536 forms zic emits: `<+06>-6`, rule times `-1` or `/24`..`/167`). DST is computed by `tz_posix`, not newlib: newlib's `tzset` rejects some valid rules and then silently keeps the previous zone. newlib gets only the offset in force (`TZ=CEST-2`, `<+06>-6`) and a one-shot timer re-applies it at the next transition, so `localtime_r()`/`strftime()` stay right everywhere (no `tm_isdst` from newlib; use `svc_time_localtime()` for that). `tz_posix` also converts in any other zone (world clock, P3-07).
- **Drift calibration** (`time_drift.c`): each reference sync (phone, SNTP) compares the RTC with the reference. The RTC reads whole seconds, so it is rewritten mid-window only once it is ≥ 1 s off; the errors at rewrites are summed over a ≥ 48 h window, then the rate becomes offset steps (4.34 ppm each, positive slows the RTC; deadband 6 ppm ≈ 0.5 s/day). An error above 200 ppm (someone else set a clock) restarts the window; a manual set restarts it too. Host tests simulate ±20/50 ppm crystals: they settle within ±11 ppm (spec ±2 s/day = 23 ppm).
- **Discipline**: no 32 kHz crystal on the ESP32-S3 side, so system time runs on the RC slow clock during light sleep. Every 10 min (`skip_unhandled_events`: never wakes light sleep) and on screen-on (at most once a minute) the system clock is compared with the RTC and stepped if they disagree by more than 1 s (the RTC estimate is reading + 0.5 s minus the known RTC error since the last sync).
- `SVC_TIME_EVT_CHANGED {what: CLOCK|ZONE|FORMAT, source, valid, h24, utc_offset_s}` after a set, a discipline step, a zone/DST change or a 12/24 h change; `app_main` maps it to `ui_clock_set_valid/_24h/_refresh` on the UI task. No task of its own: work runs in the caller, on the bus task (settings/power events) or in `esp_timer` callbacks.
- Pure parts host-tested: `host_test/test_tz_posix.cpp` (DST transitions in both hemispheres, 30/45 min offsets, `Jn`/`n`/`Mm.w.d`, negative and 24 h rule times, all-year DST, malformed strings, and an hour-by-hour sweep 2024–2030 against the host libc), `host_test/test_time_drift.cpp`. Console: `time` (state, `set`, `sync`, `tz`, `24h`, `check`, `local`, `drift reset`).
- `power_state`: battery %, mV, charging, VBUS, screen state, power profile.
- `activity`: today's steps, distance, kcal, active minutes, goal, per-minute buckets.
- `notify_store`: ring of last 50 notifications (title, body ≤ 1 KB, app id, icon id, actions, timestamp) in PSRAM, persisted on change (debounced 5 s) to `/flash/notify.bin`.
- `media_state`, `weather_state`, `calendar_state`, `call_state`, `link_state`.

### Settings (P2-02, `components/svc_settings`)
- Every setting is declared once in `sys_core/include/settings_schema.h` (X-macro: `INT(id, key, default, min, max)`, `BOOL(id, key, default)`, `STR(id, key, default, max_len)`), which generates the `s3w_setting_t` ids. NVS keys (≤ 15 chars, checked at compile time) are the persistent identity: never reuse one or change its type.
- Stored in NVS namespace `s3w_set` (ints and bools as `i32`, strings as `str`). `svc_settings_init()` (boot step 3, after `svc_worker_start()`) does `nvs_flash_init()` and loads all keys; missing keys use the default, invalid ones (out of range, too long) use the default and are rewritten. Strings are read straight into their RAM slot (no pool-sized stack buffer; the longest is `FACE_CONFIG`, 511 characters). If NVS fails, settings stay at defaults in RAM.
- `svc_settings_get_*/set_*` are safe from any task including UI: a set validates (`ESP_ERR_INVALID_ARG` / `ESP_ERR_INVALID_SIZE`), updates the RAM copy, posts `SVC_SETTINGS_EVT_CHANGED {id}` and queues one coalesced NVS write + commit on `svc_worker`. Setting the current value does nothing. A change made less than a worker turn before power loss can be lost.
- `svc_settings_factory_reset()` puts every setting at its default, erases only the `s3w_set` namespace (PHY calibration and BLE bonds live in other namespaces; the F19 factory-reset flow clears bonds and `storage` separately) and posts `SVC_SETTINGS_EVT_RESET`.
- The pure store (`settings_store.c`: schema tables, validation, load/flush against a backend) is host-tested against a fake NVS (`host_test/test_settings_store.cpp`). Console: `settings list|get|set|reset|factory`.

## 6. Memory budget

| Consumer | Region | Size |
|---|---|---|
| LVGL draw buffers (2 × 410×40 px RGB565, DMA) | internal | 2 × 32 KB |
| Instruction cache 32 KB (default 16 KB; P2-04) | internal | +16 KB |
| LVGL heap (`LV_USE_STDLIB_MALLOC = CUSTOM` → PSRAM) | PSRAM | 2 MB |
| NimBLE | internal | ~60 KB |
| Wi-Fi (when on) | internal + PSRAM | ~60 KB internal |
| Audio buffers | PSRAM | 128 KB |

Free internal heap must stay > 40 KB at all times (measured P2-04: 46.7 KB minimum in the dev build, 53.0 KB in release, through `factory test auto` with BLE + Wi-Fi up; P2-06: 40.0 KB dev, 47.0 KB release after `svc_power` and light sleep, see §7); `svc_diag` logs a warning and records a metric if not. Run with `CONFIG_SPIRAM_USE_MALLOC` and `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`.

### Display pipeline (P2-04)

Measured on hardware with `lcd bench scroll` (40-item `lv_list`, 6 px per frame) and `lcd bench swipe` (two full-screen pages with gradient, 48 px text and buttons, 1/16 width per frame). Both redraw the whole 410×502 screen every frame. The panel's TE is ~60 Hz and each frame starts on a TE pulse, so the possible rates are 60, 30, 20, 15 … fps.

| Variant (app -O2 unless noted) | Scroll fps | Swipe fps | Min internal heap |
|---|---|---|---|
| Partial 48 lines, default caches (I 16 KB / D 32 KB) | 19.9 | 20.4 | 56.1 KB |
| + D-cache 64 KB, or 64 B lines | 19.9 | 20.4 | 24.1 / 56.1 KB |
| + I-cache 32 KB | 29.9 | 29.9 | 39.7 KB |
| + I-cache 32 KB + D-cache 64 KB | 29.9 | 29.9 | 7.7 KB |
| Direct, one PSRAM frame buffer, bounce copy-out (I32/D64) | 19.9 | 19.9 | — |
| Partial, QSPI 80 MHz (I32/D64) | 29.9 | 29.9 | — |
| Partial, 2 LVGL draw threads (I32/D64) | 29.9 | 29.9 | — |
| **Partial 40 lines, I-cache 32 KB (chosen)**, release | 29.9 | 29.9 | 53.0 KB |
| Same, dev build (app -Og, LVGL -O2) | 29.9 | 29.9 | 46.7 KB |
| Same, dev build, LVGL -Og | 25.3 | 20.3 | — |
| Partial 32 lines, I-cache 32 KB | 29.9 | 29.8 | 68.5 KB |

What we learned:
- LVGL rendering is limited by instruction-cache misses (its code runs from flash). Doubling the I-cache cut render time from ~40 to ~23 ms per frame. That is the only change that moved the fps step.
- Direct mode is slower: LVGL renders into PSRAM, and the byte-swapped copy into the DMA bounce buffers (8 ms) plus the transfer (14 ms) cannot overlap rendering. In partial mode, band N goes out by DMA while band N+1 renders in internal RAM. Direct mode stays available (`S3W_LVGL_RENDER_DIRECT`) for re-measuring.
- An 80 MHz QSPI clock does not raise the rate, but it doubles the slack before the next TE pulse (8 ms instead of 4 ms). It is the first thing to try if heavier screens fall to 20 fps. Check `lcd bars`/`lcd demo` for artifacts before adopting it.
- A full-screen frame costs ≈ 23 ms render + 4 ms swap + 2 ms DMA wait. 60 fps would need all of that under 16.7 ms.
- For the P2-05 theme: changing the state of a scroll container restyles and re-lays-out all its children (+11 ms per change with 40 items). Never toggle states or styles per frame during scrolling (LVGL's own drag scrolling does not). The bench list is square without `clip_corner` (the default theme's rounded clipping cost a further ~7 ms per frame at default caches).

## 7. Power management

### States
| State | Screen | CPU | Radio | Entered when | Exit |
|---|---|---|---|---|---|
| ACTIVE | on, user brightness | 160–240 MHz | BLE connected (15–30 ms interval while transferring) | interaction | inactivity timeout (default 10 s, apps can extend) |
| DIM | on, 30 % brightness | | | 3 s before timeout | touch → ACTIVE |
| AOD | low-power face, 1 update/min | light sleep between updates | BLE slow interval (≥ 500 ms) | timeout, AOD enabled | raise / tap / button |
| SLEEP | panel sleep-in, touch monitor mode | auto light sleep | BLE slow | timeout, AOD disabled | raise / tap / button / notification / alarm |
| SAVER | as SLEEP, fewer features | light sleep; BLE off except 5-min sync window | | battery < 10 % or user | charger / user |
| WATCH-ONLY | time-only face once a minute, otherwise deep sleep | deep sleep | off | battery < 3 % or user | PWR key; charger |
| OFF | — | — | — | long-press PWR menu → off | PWR key |

### Mechanisms
- `CONFIG_PM_ENABLE`, `CONFIG_FREERTOS_USE_TICKLESS_IDLE`, DFS 40–240 MHz. Services take `esp_pm_lock` only while busy.
- BLE: request connection interval 15–30 ms while active transfer; 400–1000 ms + slave latency 4 when idle (phone side requests `CONNECTION_PRIORITY_LOW_POWER`).
- IMU: accelerometer at 25 Hz low-power, FIFO watermark ~ every 4 s, gyro off except during games/workouts. Wake-on-motion for raise detection.
- Touch: monitor mode when screen off.
- Audio rail and PA off unless sound playing; codec powered down `SVC_AUDIO_IDLE_CLOSE_MS` (1 s) after the last sound (P3-11; 🔧 check the reopen latency of the first click on hardware).
- SD card unmounted and rail off (if separable) when idle 30 s.
- Wi-Fi off by default; turned on for OTA/store/HA with DTIM-based modem sleep.
- Battery estimator in `svc_power`: logs average current per state (AXP2101 fuel gauge delta) to tune budgets.

### Implemented (P2-06, `components/svc_power`)
- **State machine** `power_fsm.c` (pure C, host tests `test_power_fsm`): ACTIVE → DIM 3 s before the timeout (`SCREEN_TIMEOUT_S`, default 10 s) → AOD (setting `AOD`) / SLEEP / SAVER (setting `BATTERY_SAVER`, wins over AOD). Touch, buttons and `svc_power_wake()` go back to ACTIVE. Holders (`svc_power_hold_screen`, the top screen's `UI_SCREEN_KEEP_ON`) stop the timeout; a finger still down at the deadline counts as activity. Two consecutive battery reads ≤ 3 % without USB power → WATCH-ONLY. WATCH-ONLY and OFF are left only by a reboot.
- **Screen on** (ACTIVE/DIM) holds `ESP_PM_CPU_FREQ_MAX` + `ESP_PM_NO_LIGHT_SLEEP`. **Screen off** releases both: DFS 40–240 MHz and automatic light sleep (`esp_pm_configure` in `svc_power_start`). DIM = 30 % of the user brightness, AOD = 10 % of full.
- **Order on screen off**: touch → monitor mode; UI hook `SVC_POWER_UI_OFF` on the UI task (screens paused, pointer polling stopped, LVGL port drops frames and the UI task sleeps with no 500 ms cap); panel display-off + sleep-in; wake inputs armed; locks released. **Screen on**: locks; inputs disarmed; brightness 0, panel sleep-out (120 ms) + display-on; UI hook `SVC_POWER_UI_ON` draws a fresh frame (`lv_refr_now`); brightness up; then touch back to active (skipped if the FT3168 does not ACK: it then sits in its own idle monitor mode, docs/01 §6, and the next touch makes it active). The touch that woke the screen is ignored by LVGL until the finger lifts. The log prints `screen on in N ms (reason)`.
- **AOD** shows the active watch face's AOD variant (`wf_set_aod`, docs/04 §4a) at AOD brightness with minute updates, moved one burn-in step a minute (P3-05, docs/04 §4a). Each LVGL frame holds a no-light-sleep lock, because the TE wait needs a GPIO edge interrupt.
- **Wake inputs** (`hal_power_arm_wake`): TP_INT (if `WAKE_ON_TAP` and not in sleep or theater mode), BOOT, PWR SYS_OUT, RTC_INT, IMU INT1 (if `RAISE_TO_WAKE`, P3-05, and not in sleep or theater mode). A settings change while the screen is off re-arms them. Light sleep can only be ended by a GPIO *level*, but the drivers use edge interrupts: `bsp_wake_arm()` switches each pin to its active level with wake-up enabled, the pin's ISR puts the edge interrupt back on its first call (no interrupt storm while a button or finger stays down), and the driver re-arms it after the event (button released, finger up, RTC flag cleared).
- **GPIO isolation**: `CONFIG_ESP_SLEEP_GPIO_RESET_WORKAROUND` (on by default for the S3) disconnects every pin in light sleep. `bsp_sleep_init()` keeps PA_CTRL (low), LCD reset and CS, touch reset, SD CS and I2C out of that.
- **Buttons** are `svc_input` (P2-07, below); it calls `svc_power_wake()`, `svc_power_user_activity()` and `svc_power_screen_off()`.
- **Battery**: read at start, every 60 s and on every PMU charger/USB event; `SVC_POWER_EVT_BATTERY` on change, `SVC_POWER_EVT_BATTERY_LOW` once per threshold (15 / 10 / 3 %) until USB power returns; 10 % and 3 % also wake the screen (`SVC_POWER_WAKE_BATTERY`, refused in DND, sleep and theater mode) for their alerts (UI flows: docs/04 §4d). Plugging USB wakes the screen and ends saver.
- **History and estimate** (P3-09, `battery_hist.c`, pure C, host tests `test_battery_hist`): every reading goes into a 24 h ring of 96 × 15 min slots (last reading of the slot, percent + charging bit; RAM only, ~110 B) and an estimate anchor that restarts when charging starts or stops or the gauge moves against the direction. Minutes left = % × elapsed / % moved; to full the same with the % above 80 counted double (constant-voltage phase); unknown until 2 % and 10 min. `svc_power_battery_info()` returns the reading, the minutes and the history (Battery app); console `power` prints the estimate.
- **Battery saver**: no AOD (`power_fsm` picks SAVER), no raise to wake (`svc_sensors`), brightness at most 50 % and screen timeout at most 10 s (`load_settings`); BLE off except a sync window comes with P4.
- **Drain estimate**: on battery, each 1 % step of the fuel gauge logs `drain 87% -> 86% in N s: ~X mA (on a%, dim b%, AOD c%, off d%, light sleep e%)` (400 mAh cell). The first step after boot or unplugging only sets the reference. A step spent ≥ 95 % screen-off is the idle current.
- **WATCH-ONLY** (P3-09): the UI hook `SVC_POWER_UI_WATCH_ONLY` draws the time-only screen (docs/04 §4d) at AOD brightness, then the chip deep-sleeps with ext1 wake on PWR SYS_OUT (GPIO10, RTC IO), PA_CTRL held low, and **the panel left on**: `bsp_deep_sleep_start(timer, keep_display)` drives LCD reset and CS high as plain outputs and holds them (both RTC IOs, `gpio_hold_en`, RTC_PERIPH kept powered), so the CO5300 keeps scanning its last frame. The RTC timer wakes the chip once a minute (`watch_only.c`, pure C, host tests `test_watch_only`: aims 1.5 s after the minute, the RC clock being a few % off; a tick up to 3 s early shows the next minute) and before the next alarm. A **minute tick boot** (`svc_power_boot_kind() == SVC_POWER_BOOT_WATCH_TICK`, an `RTC_NOINIT` record with a magic) runs only `bsp_init_early`, the PMU, `sys_core`, the panel attached without reset or init (`bsp_display_new(keep_frame)`, which releases the holds) and LVGL; it redraws the screen with that minute's burn-in step (12/24 h, valid flag and the POSIX TZ come from an `RTC_NOINIT` record `app_main` writes in the hook), waits for the pixel DMA and deep-sleeps again (`svc_power_watch_only_sleep`). USB power at a tick leaves WATCH-ONLY (`svc_power_watch_only_exit()`, the boot continues normally and the charging screen shows). At ≤ 1 % (`WATCH_ONLY_DARK_PCT`) the panel goes off and the minute ticks stop (PWR and alarms only). PWR during the short tick boot is not seen (press again). Waking for good (PWR) is a reboot. **OFF**: AXP2101 power-off.
- **Alarm wake from WATCH-ONLY** (P3-07): `svc_alarm` hands the next alarm to `svc_power_set_alarm_wake()`. Deep sleep then also arms the RTC timer (`hal_power_deep_sleep(timer_us, ...)`; the RTC INT on GPIO39 cannot wake deep sleep). The timer runs on the RC slow clock, a few % off, so it fires 2 % of the wait early (at least 5 s); with minute ticks (P3-09) the first timer wake within 70 s of the alarm is the alarm boot (`SVC_POWER_BOOT_ALARM`). On that boot `svc_power_alarm_boot()` is true and `svc_power` turns the screen off at once (only the boot logo shows briefly); the watch light-sleeps until `svc_alarm` rings on time and, once the alarm is dismissed (not snoozed), re-enters WATCH-ONLY. A button, touch or charger wake clears the flag: the watch then stays on.

### Raise to wake (P3-05, `components/svc_sensors`)
- `svc_sensors` owns the IMU (task core 0, prio 12, 3 KB stack in PSRAM). Mode **off** with the screen on, in SAVER (fewer wake sources), with `RAISE_TO_WAKE` off or without an IMU; **waiting** in AOD and SLEEP: accelerometer in low-power 21 Hz wake-on-motion (80 mg, 4 blanking samples), INT1 armed as a light-sleep wake input; **window** after a wake-on-motion interrupt: accelerometer at 62.5 Hz ±4 g, read every 20 ms, until the detector ends it, then back to waiting. Interrupts without the STATUS1 WoM flag (the INT1 level change when wake-on-motion is switched on) are ignored.
- IMU INT1 toggles its level on every wake-on-motion event, so the BSP arms it for the level opposite to the one it has (`bsp_sleep.c`) and re-arms it after each interrupt.
- **Detector** (`raise_detect.c`, pure C, host tests `test_raise_detect`): samples in the watch frame (+x 3 o'clock, +y 12 o'clock, +z out of the screen). A raise = the screen pointed at least 15° outside the screen-up cone, during the window or in the last known pose before it (the window opens ~200 ms into the motion, often past the away pose of a quick raise; the pose is the previous window's last sample, or one sample taken when the screen goes off) (half angle 25/35/45° for `RAISE_SENSITIVITY` low/mid/high), then the gravity vector entered the cone and stayed there still (| |a| − 1 g | ≤ 250 mg, change between samples ≤ 120 mg) for 60 ms. Starting inside the cone never wakes (typing, steering). The window ends 1 s after the last movement or after 3 s.
- A raise calls `svc_power_wake(SVC_POWER_WAKE_RAISE)` and posts `SVC_SENSORS_EVT_RAISE`. `svc_power` counts raise wakes that reach screen-off without a touch or button press (`raise_unused`, the false-wake count for the walk test). Sleep and theater modes (P3-08, below) turn raise off.
- The IMU axis mapping is `BSP_IMU_AXIS_MAP` / `BSP_IMU_AXIS_SIGN` in `bsp_s3w.h` (measured with `sensors accel`: the chip sits face down, x toward 12 o'clock). After the accelerometer is switched on, its first ~3 samples are railed at full scale, so `svc_sensors` reads the first one 80 ms later (`ACCEL_SETTLE_MS`).
- **Console**: `sensors` (mode, cone, motion interrupts, windows, raises, false-wake count, last window's angles), `sensors accel [n]` (samples and angle from screen-up, for the axis check), `sensors trace on|off` (log every window). `imu` commands and the factory IMU test suspend `svc_sensors` while they use the IMU (`svc_sensors_suspend`); `imu off` gives it back.
- Latency: detection is 60 ms after the wrist settles (+ up to 20 ms sampling); from SLEEP the panel's 120 ms sleep-out follows (P2-06 measured 164 ms from wake request to visible), from AOD one frame.

### Alarms and timers (P3-07, `components/svc_alarm`)
- **Model** (pure C, host tests): `alarm_sched.c` (`test_alarm_sched`): 16 alarms (local hour:minute, repeat-day bits, snooze 5–15 min, label), sorted by time of day. The next occurrence is found in local time with `tz_posix` and converted to UTC: a time in a spring-forward gap rings after the gap (02:30 → 03:30), a repeated time (fall back) rings at the first. The set keeps `checked` (UTC s up to which occurrences have rung); due = an occurrence in (max(checked, now − 5 min), now], so a late deep-sleep boot still rings but a watch that was off for a day does not ring for yesterday. An alarm saved for the current minute does not ring at once. One-time alarms switch off after ringing; a clock set backwards rewinds `checked`. NVS blob (`s3w_alarm`/`alarms`, ≤ 508 B, version + CRC-32). `timer_set.c` (`test_timer_set`): 6 countdowns on a monotonic ms clock (wrap-safe), running / paused / done.
- **Scheduling** (`svc_alarm` task, core 0, prio 13, 4 KB internal stack; all API calls take a mutex and queue the slow part, so the UI task may call them): the next alarm or snooze is an `esp_timer` (wakes light sleep, sub-ms), and the PCF85063 alarm as a backup interrupt (light-sleep wake input; re-programmed only when the next alarm changes, written once at boot); countdowns have a second `esp_timer`. Re-scheduled after every change, after `SVC_TIME_EVT_CHANGED` (new zone or clock) and after ringing. No alarms while the time is unknown. At start it checks for missed occurrences (deep-sleep wake) before scheduling.
- **Ringing**: `svc_power_wake(ALARM)`, `SVC_ALARM_EVT_RING` (UI: ring screen, docs/04 §4c), `svc_sensors_watch_flip(true)`, and the ringer in `svc_audio` (`svc_audio_ring_start(SND_ALARM|SND_TIMER)`: 880 Hz beeps, alarm 4 short beeps then a pause, timer 2; volume rising from a third of `VOLUME_ALARM` to all of it over 30 s). Alarms ignore DND and Silent. Ends with `SVC_ALARM_EVT_RING_END` {reason}: snooze (button, PWR, flip, or 2 min without an answer, up to 3 times in a row), dismiss (slider), or a timer's 1 min. An alarm due while a timer rings replaces it. Without a speaker it rings silently (the ring screen still shows).
- **Flip** (`svc_sensors`, `flip_detect.c`, `test_flip_detect`): while watched, the accelerometer streams and is read every 100 ms; armed once the screen is not facing down, a flip is z ≤ −800 mg with |a| ≈ 1 g for 400 ms → `SVC_SENSORS_EVT_FLIP`. A watch already lying face down must be turned up first.
- **Console**: `alarm` (alarms, snooze, next, timers, ring count, last ring's lateness in ms, RTC armed, speaker, flips), `alarm add HH:MM [once|daily|weekdays|weekends|<digits 0=Sun..6>] [label]`, `alarm in <min>`, `alarm del|on|off <id>`, `alarm snooze|stop`, `timer <s>`, `timer stop <id>`. The P1 `rtc alarm` bring-up command takes the RTC alarm over until reboot.

### Modes: DND, sleep, theater (P3-08, `components/svc_modes`)
- **Model** (`modes.c`, pure C, host tests `test_modes`): each mode is on by hand or inside its weekly window. A window starts at `start` on each of its days (bit 0 = Sunday) and ends at `end`, the next day if `end ≤ start` (`end == start` = 24 h). Turning a mode off by hand inside its window skips the rest of that window; turning it on by hand keeps it on until it is turned off. Theater mode has no schedule. Schedules are ignored while the time is unknown.
- **What they mean**: *quiet* (DND, sleep or theater) — notifications neither sound nor wake the screen (`svc_power` refuses `SVC_POWER_WAKE_NOTIFY`; sounds and vibration: `svc_audio` P3-11 and `svc_notify` P6 read `svc_modes_get()`). *dark* (sleep or theater) — no AOD (screen off goes to SLEEP), tap and raise do not wake (`svc_power` drops TP_INT from the wake inputs and refuses TOUCH/RAISE wakes; `svc_sensors` stays off). Theater mode turning on also turns the screen off at once. Buttons, alarms, the charger and the console always wake; alarms ring in every mode.
- **Sounds** (`svc_audio`, P3-11; `audio_mix.c` is pure C, `test_audio_mix`): one task (core 0, prio 15, 6 KB) sleeps on its queue; a request opens the codec (16 kHz mono), mixes up to 4 voices in 20 ms chunks and closes the codec 1 s after the last sound. Sounds are note sequences synthesised in code (click, notify, success, charging, lowbat, alarm, timer; 5 ms edge fades), not stored samples. Policy is applied when a sound starts: system sounds use `VOLUME_SYSTEM` and are dropped by `SILENT`, by any quiet mode (`svc_modes_get().quiet`) and at volume 0; alarm and timer sounds use `VOLUME_ALARM` and ignore both. The codec volume follows the loudest active voice and quieter voices are scaled in software. `svc_audio` plays the charging sound (VBUS appears after boot) and the low-battery sound (every `SVC_POWER_EVT_BATTERY_LOW`) itself; `svc_notify` (P6) will play `SND_NOTIFY` and the UI can play `SND_CLICK` / `SND_SUCCESS`. No speaker: every call is a no-op.
- **Settings**: `DND` and `SLEEP_MODE` (by hand), `DND_DAYS/START/END` and `SLEEP_DAYS/START/END` (days 0 = no schedule; minutes of the day). Theater mode is not kept: a restart ends it.
- **Service** (no task): API calls, settings events and `SVC_TIME_EVT_CHANGED` (clock, zone, DST) recompute the state under a mutex; one `esp_timer` publishes `SVC_MODES_EVT_CHANGED {modes_state_t}` on a change, in order, and is re-armed for the next schedule edge (a few wake-ups a day at most). `svc_modes_start()` runs before `svc_power` and `svc_sensors`, which read the state at their start. Quick settings toggles DND, theater and sleep through `svc_modes_set()`.
- **Console**: `modes` (state, by hand, skips, schedules, next edge), `modes dnd|sleep|theater on|off`, `modes sched dnd|sleep HH:MM HH:MM [daily|weekdays|weekends|<digits 0=Sun..6>]`, `modes sched dnd|sleep off`.

### Input (P2-07, `components/svc_input`)
- No task: button edges arrive in the FreeRTOS timer task (bsp debounce) and each button has a one-shot FreeRTOS timer in that same task, armed only while a press is pending. Touch samples arrive from the `touch` task, which polls only while a finger is down.
- **Presses** (`button_gesture.c`, pure C, host tests `test_button_gesture`): a click is a press released before the long time; clicks less than 300 ms apart form a double or triple press. LONG is reported while still held (BOOT 1 s, PWR 2 s) and its release reports nothing. A button waits the 300 ms gap only if a double or triple press is mapped for it, so BOOT's short press is reported at once on release; PWR (triple mapped) reports short 300 ms after release, and triple at the third release.
- **A press while the screen is off** (not ACTIVE/DIM) only wakes it (`svc_power_wake(BUTTON)`); its release is ignored. A press while on restarts the screen timeout.
- **Actions** (`svc_input_set_action`, defaults per docs/03 F3): BOOT short = BACK, BOOT long = SHORTCUT, PWR short = HOME, PWR long = POWER_MENU, PWR triple = SOS. Events: `SVC_INPUT_EVT_BUTTON` (every press), `SVC_INPUT_EVT_ACTION` (mapped presses), `SVC_INPUT_EVT_PALM`. `app_main` maps the actions on the UI task: BACK = `ui_nav_back()`, HOME = `ui_nav_home()` or screen off on the home screen, POWER_MENU = power menu; SHORTCUT and SOS show a toast until their features exist (P3, P4).
- **Palm cover** (`palm_detect.c`, host tests `test_palm_detect`): the FT3168 is read in one transfer per sample (point count, two points with weight and area). A sample covers when the count is above the two tracked points (a blob), when two points span ≥ 60 % of the screen, or (off until measured) when a point's area reaches a threshold. 300 ms of covering samples while the screen is on → `svc_power_screen_off()` and `SVC_INPUT_EVT_PALM`, once per contact. Counts above 5 carry no coordinates; the touch task keeps polling them for at most 1 s. Console `input` prints press counts, the action map and the largest values of the last contact, for tuning.
- **USB console**: `CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION` holds a no-light-sleep lock while a USB *host* is connected (SOF frames), so the console works at the desk and the watch light-sleeps on battery or a plain charger. Measure sleep on battery, then reconnect and read `power` / `pm stats` (counters survive).
- **Console**: `power` (state, battery, time per state, wakes by reason, light-sleep count and time, drain estimate), `power screen on|off`, `saver on|off`, `timeout <s>`, `hold on|off`, `watchonly`, `shutdown`, `restart`; `pm stats` (esp_pm lock and mode times, `CONFIG_PM_PROFILING`, dev builds only).
- **Cost** (measured on the dev board, P2-06): minimum internal heap through `factory test auto` 40.0 KB dev / 47.0 KB release (was 46.0 / 53.0: the 4 KB `svc_power` stack — 2.2 KB used — plus PM and light-sleep state). `lcd bench scroll` 29.9 fps release, 29.4 fps dev (`CONFIG_PM_PROFILING` costs ~0.3 fps, DFS bookkeeping ~0.1). `CONFIG_PM_POWER_DOWN_CPU_IN_LIGHT_SLEEP` is **off**: it would save ~650 µA in light sleep but takes ~17 KB of internal RAM (CPU context + cache-tag backup, which IDF 5.5.1 cannot build without), leaving 24 KB — under the floor. Revisit in P10 (free internal RAM elsewhere first).
- Screen-on latency from SLEEP: 164 ms (120 ms of it is the panel's sleep-out time), from AOD: one frame.
- Remaining periodic wake-ups with the screen off: the PMU IRQ poll (2 s, `bsp_pmu.c`; the AXP2101 IRQ pin is not wired) and the battery read (60 s). In WATCH-ONLY: one short boot a minute (P3-09; none at ≤ 1 %).

### Budget (targets to validate in Phase 10)
| Item | Avg current | Hours/day | mAh/day |
|---|---|---|---|
| Screen on (mid brightness, active CPU) | ~70 mA | 1.0 | 70 |
| Idle (light sleep, BLE connected, IMU LP, touch monitor) | ~2.5 mA | 23 | 58 |
| Notifications, syncs, alarms | — | — | 15 |
| **Total (AOD off)** | | | **~145 → ~2.7 days** |
| AOD on adds ~6 mA × 23 h | | | +138 → ~1.4 days |

## 8. Storage

### Partition table (`firmware/partitions.csv`, 32 MB flash)
```
# Name,    Type, SubType,  Offset,    Size
nvs,       data, nvs,      0x9000,    0x6000
otadata,   data, ota,      0xF000,    0x2000
phy_init,  data, phy,      0x11000,   0x1000
nvs_keys,  data, nvs_keys, 0x12000,   0x1000
ota_0,     app,  ota_0,    0x20000,   0x600000
ota_1,     app,  ota_1,    0x620000,  0x600000
coredump,  data, coredump, 0xC20000,  0x10000
storage,   data, littlefs, 0xC30000,  0x13D0000
```
- Two 6 MB app slots (fonts and images compiled in); the second slot is for over-the-air updates, which are not part of the Community edition. If firmware approaches 5 MB, move bitmap assets to a versioned `assets` partition.
- `storage` (~19.8 MB LittleFS, `joltwallet/littlefs`) layout:
```
/flash/sys/         settings backups, crash metadata
/flash/faces/<id>/  declarative watch faces
/flash/logs/        rotating logs (2 × 256 KB)
```
- `/sd` (FAT32): free for the user; hot-plug tolerant.

## 9. Security
| Item | Debug build | Release build |
|---|---|---|
| Secure Boot v2 | off | on |
| Flash encryption | off | on (release mode) |
| NVS encryption | off | on (`nvs_keys` partition) |
| JTAG | on | disabled via eFuse |
| USB download mode | on | "secure download" only |

> **Warning:** Secure Boot and flash encryption burn eFuses and cannot be undone. Never enable them on the development board. Do it only in the documented provisioning script (`tools/provision_release.sh`, task P10-06) on dedicated units, with keys backed up offline.

Other: no secrets in logs.

## 10. Diagnostics
- `esp_console` over USB-Serial-JTAG (debug + optionally release behind a dev toggle). Commands: `i2c scan`, `pmu`, `imu`, `rtc`, `audio tone|rec|play|snd|ring|stop|status`, `sd`, `power`, `pm stats` (P2-06), `input` (P2-07), `time` (P3-01), `face` (P3-02), `heap` (P2-08; `heap check` walks the heap), `tasks`, `log <tag|*> <level>`, `metrics [clear]`, `coredump info|dump|erase`, `crash null|abort|intwdt`, `factory test`.
- Coredump to flash on panic (P2-08): ELF format, CRC32, into the 64 KB `coredump` partition (`sdkconfig.defaults`); kept until `coredump erase`. `coredump info` prints the crashed task, PC, exception cause and backtrace; `coredump dump` prints the image as base64 between `CORE DUMP START/END` markers. Decode on the host: save the lines between the markers to `dump.b64`, then `espcoredump.py info_corefile -t b64 -c dump.b64 build/s3wear.elf`. `crash null|abort|intwdt` forces a panic to test it.
- Metrics ring buffer (P2-08, `svc_diag/metrics_ring.c`, pure C, host tests `test_metrics_ring`): boot count, per-reason reset counts, minimum free heap (all boots, this boot, internal RAM) and a 32-entry ring of events: BOOT (reset reason + previous boot's minimum free heap), DRAIN (power state + mA, recorded when the last 1 % step changes). Stored as one CRC-protected NVS blob (`s3w_diag`/`metrics`, 0.6 KB), loaded and counted by `svc_diag_metrics_start()` at boot step 3, saved on `svc_worker` every 15 min (`esp_timer`, `skip_unhandled_events` so it never wakes light sleep) and at boot. A crash loses at most the last 15 min of heap/drain data, never the boot count or reset reason. Console `metrics`.
- Task watchdog on UI and service tasks.

## 11. Simulator
- Now (P2-03, P2-05): the simulator builds `ui_framework/ui`, `watchfaces` and `hal_sim` and runs the same boot steps as `app_main` minus board bring-up (RTC time, theme, boot screen, brightness, HAL pointer, button and PMU callbacks, home screen). `svc_power` and `svc_input` do not run there yet: the HAL power calls are no-ops and buttons map straight to navigation (BOOT = back, POWER = home, on press); the power menu opens with `push power`. Keys: Esc/Backspace = BACK, P = POWER (home), C = charger in/out; `$S3W_SIM_SD` names a host directory that acts as the SD card. Watch faces get the battery from `hal_sim` and a fixed demo data set (steps, weather, next event, …) standing in for the phone link (`simulator/sim_data.c`); `--faces <dir> [--face <id>]` loads declarative faces from `<dir>/<id>/face.json` (P3-03). The clock apps (P3-07) run over `simulator/sim_clock.c`, an in-memory backend on the same `alarm_sched`/`timer_set` code as `svc_alarm` (demo alarms 07:00 daily and 08:30 weekends off; world clock Tokyo, London, New York); timers run on the LVGL tick and ring by themselves, alarms ring from the script (`ring alarm`); POWER snoozes while ringing. DND, sleep and theater modes (P3-08) run on `modes.c` in `simulator/sim_modes.c` on the pinned clock (script `mode`, `sched`); they show in quick settings only (no `svc_power` there). The battery screens (P3-09) run over `simulator/sim_battery.c` (hal_sim's fake battery, a demo 24 h history on `battery_hist.c`, saver shared with quick settings); the script plays `svc_power`'s events (`battery`, `charger in|out`, `low 15|10|3`) and draws the WATCH-ONLY screen (`watchonly`). `sys_core` and services join as they get host ports.
- Headless scenarios (P2-05): `--script <file>` runs a scenario with a pinned clock and scripted touch/keys, `--screenshot` writes the final frame, `--expect <golden.png>` fails on any pixel difference. `ctest --test-dir build/sim` runs every `firmware/test/ui/*.txt` against `firmware/test/ui_snapshots/` (format and update script: `firmware/test/ui/README.md`).
- `firmware/simulator/` is a CMake project using LVGL's SDL driver, compiling `ui_framework`, `ui_apps`, `watchfaces`, `sys_core` and service logic with `hal_sim` (fake time, mouse = touch, keys for buttons, fake battery).
- Window is 410×502 with optional watch bezel mask so designs match real proportions.
