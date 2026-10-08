# 03 — Firmware feature specifications

Each feature: behaviour, hardware/services used, settings, edge cases, acceptance criteria (AC). This is the Community edition: the standalone watch (roadmap Phases 0–3). Features that need a phone are not part of it.

---

## F1. Time & watch faces
**Behaviour**
- System time from PCF85063 at boot; synced from phone on every connect and every 6 h (protocol `TimeSync`), and from SNTP when Wi-Fi is on. RTC re-written after each sync. Drift correction: store measured drift, apply PCF85063 offset register.
- Timezone as POSIX TZ string from phone; DST computed by `svc_time` (`tz_posix`), which hands newlib only the current offset and re-applies it at each transition (newlib cannot parse every valid rule). Time unknown (RTC lost power, never synced) → clocks show `--:--` with a "Time not set" hint. Implementation: docs/02 §5 "Time".
- Watch faces: 4 native faces in v1 (Digital Bold, Analog Classic, Info-dense "Modular", Minimal) + declarative faces installed from the phone.
- Complications (slots defined by each face): battery, steps + ring, date, weather, next calendar event, sunrise/sunset, world time, phone battery, heart rate (from phone/add-on), alarm, timer countdown, notification count, moon phase.
- Tap a complication → opens its app. Long-press face → face picker (swipe between faces, "customize" for slot/colour choice).
- Each face has an **AOD variant** (outline digits, no seconds, ≤ 10 % lit pixels).

**Declarative watch face format** (`face.json` + PNG/bin assets, packaged `.s3face`), api 1:
```json
{
  "id": "com.example.neon", "name": "Neon", "version": "1.0.0", "api": 1,
  "background": "#000000",
  "elements": [
    {"type": "arc",  "bind": "steps.goal_ratio", "x": 205, "y": 251, "r": 195, "w": 8, "start": 135, "end": 405, "color": "#76FF03"},
    {"type": "text", "bind": "time.hh:mm", "x": 205, "y": 200, "font": "digits_96", "color": "#00E5FF", "align": "center"},
    {"type": "text", "bind": "date.EEE d MMM", "x": 205, "y": 280, "font": "body_26", "upper": true},
    {"type": "hand", "bind": "time.second", "len": 176, "tail": 32, "w": 3, "color": "#FF453A"},
    {"type": "complication", "slot": "bottom", "x": 205, "y": 404, "default": "battery"}
  ],
  "aod": {"elements": [ {"type": "text", "bind": "time.hh:mm", "x": 205, "y": 231, "font": "digits_96_light", "color": "#9A9AA0"} ]}
}
```
Element types `text`, `arc`, `hand`, `circle`, `rect`, `ticks`, `image`, `complication`; every field, default and limit is in `watchfaces/include/wf_decl.h`. Bindings come from a registry (`time.*`, `date.*`, `battery.*`, `steps.*`, `weather.*`, `phone.*`, `next_event.*`, `heart.*`, `notifications.*`, `moon.*`; `wf_bind.h`), so faces cannot run code. The parser is strict: unknown keys or types, duplicate keys, wrong types, values out of range, unknown bindings/fonts/complications and files over 16 KB are errors with the element path and line:column (`elements[2].font: unknown font 'huge'`).

Implemented (P3-02): engine, complication and binding registries, the 4 native faces and their AOD variants — docs/04 §4a. (P3-03): face.json parser and renderer, two sample faces built into the firmware (`s3w.neon`, `s3w.dial`), installed faces loaded from `/flash/faces/<id>/face.json` at boot — docs/04 §4a. **Not yet**: images (`background` file, `image` elements, image hands) are validated but not drawn, because decoded PNGs need an LVGL image cache in PSRAM that the watch build does not have yet; image hands fall back to the vector hand. Installing `.s3face` packages from the phone comes with the phone link (P4+). (P3-04): long-press picker with live previews, customize (complication per slot, colour), saved in `WATCH_FACE` / `FACE_CONFIG` — docs/04 §4a.

**AC**: Time correct after reboot without phone; ±2 s/day after drift calibration; switching faces < 300 ms; AOD face lights < 10 % pixels (measured by a simulator test that counts non-black pixels).

## F2. Wake / sleep behaviour
- Raise-to-wake: IMU wake-on-motion IRQ → `svc_sensors` runs a 1 s accel window: wrist rotation toward user (gravity vector moves into the screen-up cone ±35°) → `SCREEN_ON`. False wake rate goal < 10/hour while walking.
- Tap-to-wake (FT3168 monitor mode), double-tap optional; PWR key wakes.
- Palm cover (touch area > 60 % of screen) → screen off.
- Screen-off timeout 5/10/15/30 s; apps can hold "keep screen on" (e.g. workout, stopwatch running visible).
- Flip-to-mute alarms/calls: face-down detection.
Implemented (P3-05): raise to wake in `svc_sensors` (wake-on-motion → 1–3 s accel window → cone test, docs/02 §7 "Raise to wake"), tap to wake and PWR (P2-06), AOD burn-in shift (docs/04 §4a). Raise is off in battery saver. Sleep and theater modes (P3-08, F4): no AOD, tap and raise do not wake, buttons do; theater turns the screen off at once. **Not yet**: double-tap, flip-to-mute for calls (P5).

**AC**: raise → visible < 150 ms from motion end (screen was in sleep); no wake when DND+theater mode.

## F3. Navigation & system UI
See 04-ui-ux.md. Watch face is home. Swipe down = quick settings; up = notifications; left = widget "tiles"; right = launcher (or button BOOT). PWR short = home/screen off; PWR long (2 s) = power menu; BOOT short = back; BOOT long = configurable shortcut.
Implemented (P3-06): the four swipes, the launcher on BOOT, docs/04 §4b. Notifications is an empty placeholder in this edition. **Not yet**: BOOT long shortcut.

## F4. Quick settings
Toggles: DND, Theater (screen stays off until button), Sleep mode, Brightness slider, AOD, Wi-Fi, Bluetooth, Silent, Flashlight, Battery saver, Find phone, Settings. Shows battery % and phone connection.
Implemented (P3-06, docs/04 §4b): panel and backend; DND, AOD, Silent, saver and brightness change their settings. Implemented (P3-08, `svc_modes`, docs/02 §7 "Modes"): DND, Sleep and Theater toggles; DND and sleep mode also on weekly schedules (settings `DND_*`, `SLEEP_*`; console `modes sched`; turning one off inside its window skips the rest of it). DND, sleep and theater stop notification wake-ups; sleep and theater also AOD, tap and raise wake. **Not yet**: Wi-Fi (P9), Bluetooth and the phone link (P4), sounds and vibration following DND/Silent (P3-11), schedule editing on the watch (Settings app P3-10) and from the phone (P6), a mode icon on the face; Flashlight, Find phone and Settings open apps that do not exist yet.

## F5. Battery & charging
- Battery %, mV, charging state, time-to-full estimate, last 24 h drain graph (from svc_power metrics).
- Charging screen when VBUS connected and screen wakes: big percentage + animation; watch face still reachable.
- Low battery 15 % (toast + sound), 10 % offer saver, 3 % → watch-only mode.
- AXP2101 die temperature monitoring: if > 60 °C pause charging (AXP has its own limits; this is a software guard).

Implemented (P3-09, docs/04 §4d, docs/02 §7): Battery app (level, state, time left / to full from the average rate since charging started or stopped, 24 h graph from a RAM history in `svc_power`, saver switch, watch only, voltage); charging screen on plug-in and on screen wake while on USB power; 15 % toast, 10 % saver offer, 3 % watch-only alert (10 % and 3 % wake the screen unless DND, sleep or theater mode is on). Battery saver: no AOD, no raise to wake, brightness at most 50 %, screen timeout at most 10 s; USB power ends it. WATCH-ONLY shows the time and battery % once a minute with the chip in deep sleep between; PWR leaves it (reboot), USB power leaves it within a minute; at ≤ 1 % the panel goes off too. **Not yet**: the 15 % sound and charging sound (P3-11), the die temperature guard, BLE off in saver (P4), the history across reboots, the empty-battery boot check (docs/02 §2 step 2).

## F6. Alarms, timers, stopwatch, world clock
- Alarms: up to 16; repeat days; label; sound; snooze (5–15 min); smart wake window (wake in light sleep within 15–30 min window using sleep stage data — P2).
- Scheduling: next alarm programmed both in `esp_timer` (for light sleep) and PCF85063 alarm (time source of truth). In WATCH-ONLY deep sleep, ESP RTC timer is the wake source.
- Alarm ring: speaker with escalating volume, full-screen UI, dismiss requires swipe; flip-to-snooze. Alarm rings even with no phone, in DND (alarms bypass DND), and is synced to/from phone.
- Timers: multiple concurrent; presets; complication shows countdown.
- Stopwatch with laps (keeps screen on optional).
- World clock: up to 6 cities (tz from phone city list).
**AC**: alarm fires within 1 s of set time from SLEEP and from WATCH-ONLY; persists across reboot.
Implemented (P3-07, `svc_alarm`, docs/02 §7, docs/04 §4c): up to 16 alarms with repeat days, label (from the console until the phone link), snooze 5/10/15 min; DST-aware scheduling (a time in the spring-forward gap rings after the gap, a repeated time rings once); `esp_timer` + PCF85063 alarm, and the ESP RTC timer for WATCH-ONLY (woken early by 2 % of the wait, then the screen stays off until the alarm and the watch goes back to WATCH-ONLY after it is dismissed); occurrences missed by ≤ 5 min (slow boot) still ring. Ring screen with snooze and slide-to-stop, PWR snoozes, flip face down snoozes, unanswered alarms snooze themselves after 2 min (3 times); beeps with rising ALARM volume, ignoring DND and Silent. Up to 6 timers (presets 1–30 min and custom, pause/resume/restart; a done timer rings for 1 min; not kept across a reboot), timer complication. Stopwatch with laps (30 kept; no keep-screen-on option yet). World clock with up to 6 cities from a built-in list of 24 (setting `WORLD_CLOCKS`; the first feeds the world time complication). **Not yet**: smart wake (P2), alarm sync with the phone (P4/P6), sound choice, alarm label editing on the watch (keyboard), the phone's city list (P4).

## F7. Audio
- System sounds (click, notification, alarm, timer, charging, low battery, success); volume per category; Silent mode. Implemented (P3-11, `svc_audio`, docs/02 §7): sounds are note sequences synthesised at 16 kHz (no ADPCM/WAV assets yet), up to 4 mixed at once; categories system (`VOLUME_SYSTEM`; muted by Silent and by DND, sleep and theater) and alarm (`VOLUME_ALARM`; never muted); media (`VOLUME_MEDIA`) is reserved for the player (P2). Plays the charger and low-battery sounds itself; console `audio snd <name>`, `audio status`. **Not yet**: UI click sounds and the "haptic substitute" (no setting or hook yet), sound choice, ADPCM/WAV assets.
- "Haptic substitute": very short soft clicks on UI events (optional, default off).

## F8. Settings (on watch)
Display (brightness, timeout, AOD, raise-to-wake sensitivity, wake on tap), Sound (volumes, silent), Notifications (DND schedule, sleep mode schedule), Watch faces, Connections, Battery (saver, stats), Accessibility (large text, bold, high contrast), Language & region, System (about, storage, restart, power off, factory reset, developer options: console, logs, FPS overlay).

Implemented (P3-10, docs/04 §4e): the tree with Display, Sound, Notifications (schedules), Watch faces, Connections, Battery, Accessibility, Language & region and System (About, Developer options, Restart, Power off, Factory reset). **Not yet**: Language other than English, the effect of Large text and High contrast (the settings are stored), Developer options taking effect (console, FPS overlay), storage in Factory reset.

