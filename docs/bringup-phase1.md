# Phase 1 bring-up: hardware checks

Manual checks for the 🔧 acceptance criteria of P1-01 … P1-09 ([09-roadmap-tasks.md](09-roadmap-tasks.md)). The firmware builds and the host tests pass in CI; everything below needs the real board. Record results (date, board, fw version) at the end of this file.

## Setup

```bash
. $IDF_PATH/export.sh
cd firmware && idf.py build && idf.py -p /dev/cu.usbmodem* flash monitor
```

The console prompt is `s3w>`; `help` lists every command. Log lines look like `I (1234) tag: message`; the timestamps vary.

Expected boot log (order matters, values vary):

```
I (...) app_main: S3Wear boot (<version>, IDF v5.5.1)
I (...) bsp_s3w: early init done (I2C 400 kHz)
I (...) bsp_pmu: power-on source 0x.., last power-off source 0x..
I (...) svc_time: system time from RTC: 2026-..-.. ..:..:.. UTC      # or: RTC time invalid (oscillator stopped or no RTC): time unknown
I (...) bsp_display: CO5300 410x502, QSPI 40 MHz
I (...) lvgl_port: LVGL 9.3.0, 410x502, 2 x 52480 B draw buffers
I (...) bsp_touch: FT3168 chip id 0x03 fw 0x..
I (...) svc_storage: /flash: LittleFS .. KB used of 20288 KB
I (...) svc_storage: no SD card (...)                                  # or: /sd: FAT .. MB used of .. MB
```

No `... failed:` lines from `app_main`. The screen shows **S3Wear** in white on black.

## P1-01 — I2C bus and buttons

1. `i2c scan`
   ```
     0x18  ES8311
     0x34  AXP2101
     0x38  FT3168
     0x40  ES7210
     0x51  PCF85063
     0x6B  QMI8658
   6 device(s) found, 6/6 expected on-board devices present
   ```
2. Press and release BOOT: `app_main: button BOOT down`, then `... up`.
3. Short-press PWR: `app_main: button PWR down` / `up` and `cmd_pmu: PMU event: PWR short press`.
4. Fill in the "Hardware check" line in [01-hardware.md §3](01-hardware.md).

## P1-02 — Display and LVGL

1. `lcd bars` — eight vertical bars (white, yellow, cyan, green, magenta, red, blue, black) and a 3 px **red frame on all four edges**. A missing or doubled edge means the panel gap is wrong.
2. `lcd bright 0`, `lcd bright 255`, `lcd bright 128` — brightness changes, nothing else.
3. `lcd off` (screen dark), `lcd on` (image back, unchanged).
4. `lcd demo` — LVGL widgets demo renders without offset; scroll it by touch (after P1-03) and look for tearing.
5. `lcd fps` → `full-screen refresh: 60 frames in NNNN ms -> NN.N fps`. Record the value in [01-hardware.md §5](01-hardware.md). Repeat with menuconfig → *S3Wear board* → QSPI clock 80 MHz; keep 80 only if `lcd bars`/`lcd demo` show no artifacts.

## P1-03 — Touch

1. `touch info` → `FT3168 id 0x03 (FT3168) fw 0x.. pmode 0, released (0,0)`.
2. `touch draw` — dots follow the finger. Swipe each direction: `cmd_touch: swipe left|right|up|down`. Hold still ~1 s: `cmd_touch: long-press at X,Y`.
3. `touch monitor 30` → `screen off, touch in monitor mode; tap within 30 s`. Tap: screen comes back and `woke by tap: screen on NNN ms after touch-down` (expect ≈ 120–200 ms: sleep-out needs 120 ms).

## P1-04 — RTC

1. `rtc set 2026-10-02 12:00:00` → `set 2026-10-02 12:00:00 UTC`. `rtc get` shows the RTC and system time a few seconds later, `oscillator-stop flag: clear`.
2. Reset (unplug/replug USB or `pmu off` + PWR). Boot log shows `system time from RTC: ...` with the time still running.
3. `rtc alarm +60` → `alarm set for ... UTC`; 60 s later `cmd_rtc: RTC alarm interrupt at hh:mm:ss UTC`.
4. `rtc offset` → `offset: 0 steps (normal, 4.34 ppm/step)`.

## P1-05 — PMU

1. `pmu` prints VBUS, battery %, mV, charger state, VSYS, die temperature, `charge cfg: CC 200 mA, term 25 mA, pre 50 mA, CV 4200 mV`, `PWR hold  : 6 s = power off` and the rails. Expect `DCDC1 ON 3300mV` and `ALDO1 ON 3300mV` (update [01-hardware.md §4](01-hardware.md) if not).
2. Unplug USB, plug it back: within 2 s `cmd_pmu: PMU event: VBUS inserted` (and `charging started` unless the battery is full). The console reconnects after replugging.
3. On battery (USB unplugged): hold PWR for 6 s → the watch switches off. Press PWR ~1 s → it boots.
4. `pmu rail ALDO1 off`, then `audio tone 1000 500` is silent; `pmu rail ALDO1 on` restores sound. (Confirms the audio rail mapping.)

## P1-06 — IMU

1. Watch flat, face up: `imu stream` → `az mg` ≈ +1000 (±60), `ax`/`ay` ≈ 0, gyro ≈ 0 dps. Turn it on its side: 1000 mg moves to x or y.
2. `imu selftest` → `accel self-test: PASS ...` and `gyro  self-test: PASS ...`.
3. `imu steps 60` and walk: `steps: N` goes up in steps of 4 after the first 10 steps; `cmd_imu: pedometer IRQ: N steps` lines appear. `imu off`.
4. `imu wom 100`, leave it still, then move it: `cmd_imu: wake-on-motion IRQ`. `imu off`.
5. `imu tap`, tap the case: `cmd_imu: single tap on +z` (double-tap → `double tap`). `imu off`.
6. `imu fifo 5` → about every 0.5 s `cmd_imu: FIFO watermark: 32 frames, first accel raw ...`.

## P1-07 — Audio

1. `audio tone 1000 500` — a clear 1 kHz beep for half a second. `audio vol 30` / `audio vol 90` and repeat: quieter/louder.
2. `audio rec 3`, speak → `recorded 48000 frames, RMS mic1 N mic2 M` (both clearly above the silent value). `audio play` plays it back.
3. `audio loop` → `mic1: PASS (...)`, `mic2: PASS (...)`, `loopback 1000 Hz: PASS`.

## P1-08 — Storage

1. Boot without a card: `svc_storage: no SD card (...)`, everything else works.
2. `fs df`, `fs write /flash/hello.txt hi there`, `fs ls /flash`, `fs cat /flash/hello.txt`, `fs rm /flash/hello.txt`.
3. Insert a FAT32 card, `sd mount` → `mount: ESP_OK` and the card info; `fs ls /sd`. Pull the card, `sd info` → `no SD card mounted` and `svc_storage: SD card stopped answering: unmounted`.

## P1-09 — Factory test

1. Insert a TF card and be near a Wi-Fi access point.
2. `factory test` — the screen shows the checklist; follow the yellow prompts (press BOOT, press PWR briefly, tap the screen).
3. Expected: every row PASS, `ALL PASS` on screen, and one JSON line with `"pass":true,"skipped":0`.
4. Check the `factory_test: internal heap: ... B minimum since boot` line: the minimum must stay above 40960 B (docs/02 §6). If BLE or Wi-Fi fail with `ESP_ERR_NO_MEM`, internal RAM is the problem — reduce `LCD_BUF_LINES` in `main/app_main.c` and report it.

## Results

| Date | Board | FW | Result | Notes |
|---|---|---|---|---|
| 2026-10-02 | dev board, BT MAC 80:b5:4e:da:6f:a6, 16 GB SD | cad74d8 + bring-up fix commit | Factory test 14/14 PASS (interactive run + auto run) | i2c 6/6; display TE 60 Hz, 29.8 fps full-screen; touch id 0x03; RTC alarm IRQ on time; PMU charger 200 mA; IMU self-test PASS, \|a\| ≈ 1000 mg, FIFO watermark IRQ, tap; audio loopback SNR ≈ 60 dB both mics; /flash + /sd; BLE advertise; Wi-Fi 6–8 APs. Min internal heap 40.4 KB (just above the 40 KB budget). Hand checks: colour bars + 3 px red frame visible on all edges (1 px is too thin to see), swipes 4 directions, tap, tap-to-wake 119 ms, BOOT/PWR buttons, beep audible, voice recorded (RMS ~870/1250 vs ~260 noise), wake-on-motion IRQ. Hardware pedometer: **FAIL** — 0 steps after ~40 real steps untethered (config correct, no reboot) → software step detector in P5-01. **Open:** 6 s hard-off; USB plug event log (watch survived the last unplug/replug). |
