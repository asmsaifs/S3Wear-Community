# CLAUDE.md — rules for AI agents in the S3Wear repo

Read [PLAN.md](PLAN.md) first, then the doc for the area you are changing. Tasks live in [docs/09-roadmap-tasks.md](docs/09-roadmap-tasks.md); always say which task ID you are working on.

## Workflow
1. Restate the task ID, files you will touch, and acceptance criteria before writing code.
2. Make the smallest change that meets the criteria. Do not start the next task.
3. Build and test (commands below). Paste the relevant output when you report.
4. If a criterion needs real hardware, write the exact manual test steps and expected console output; do not claim it passed.
5. Update docs if you changed behaviour, an API, the protocol or the partition table.

## Commands
```bash
# Firmware (ESP-IDF v5.5.x must be exported: . $IDF_PATH/export.sh)
cd firmware && idf.py set-target esp32s3 && idf.py build
idf.py -p /dev/cu.usbmodem* flash monitor
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.release" build   # release profile

# Host unit tests (pure logic)
cmake -S firmware/host_test -B build/host_test && cmake --build build/host_test && ctest --test-dir build/host_test

# Simulator (SDL2)
cmake -S firmware/simulator -B build/sim && cmake --build build/sim && ./build/sim/s3w_sim
./build/sim/s3w_sim --screenshot out.png --script firmware/test/ui/<scenario>.txt   # headless snapshot
ctest --test-dir build/sim                   # UI snapshot tests; goldens: firmware/test/ui/update_snapshots.sh

# UI fonts (after changing sizes/ranges; needs Node.js)
tools/fonts/gen_fonts.sh

# Protocol codegen (after editing protocol/proto)
tools/gen_proto.sh

# Android
cd android && ./gradlew ktlintCheck detekt test assembleDebug   # `test` also covers pure-JVM modules (:core:protocol)
```

## Firmware rules
- Pin numbers and I2C addresses exist **only** in `components/bsp_s3w/include/bsp_s3w_pins.h`. Everywhere else uses the BSP or HAL.
- Use the new I2C driver (`driver/i2c_master.h`). Never use the legacy `driver/i2c.h`. The bus is shared by 7 devices; every device gets its own `i2c_master_dev_handle_t` on the one bus handle from the BSP.
- Never use `LV_LABEL_LONG_MODE_DOTS` (it rewrites the label text while the render thread draws it: endless loop in `lv_draw_label`); use `s3w_label_fit()` / `s3w_label_set_fit_text()` from `ui_widgets.h`.
- **LVGL is single-threaded.** Only the UI task touches LVGL objects. Other tasks post to the UI via `ui_post(fn, ctx)` or the event bus. If you must call LVGL from elsewhere, wrap in `lv_lock()/lv_unlock()`.
- Never block in the UI task: no I2C, no flash writes, no BLE calls, no `vTaskDelay`. Ask a service and receive the result via event.
- Memory: allocations > 4 KB use PSRAM (`heap_caps_malloc(n, MALLOC_CAP_SPIRAM)`) unless DMA or ISR access needs internal RAM. Images and fonts are `const` (in flash).
- Errors: return `esp_err_t`; use `ESP_RETURN_ON_ERROR`/`ESP_GOTO_ON_ERROR`. No `ESP_ERROR_CHECK` outside `app_main` boot steps.
- Logging: `static const char *TAG = "svc_power";` One TAG per file. No logging in ISRs. Debug logs compiled out in release.
- Any periodic timer or task wake-up must state its period and justification in a comment; prefer event-driven.
- Services expose a C API in `include/svc_<name>.h` and publish events defined in `include/svc_<name>_events.h`.
- Settings keys are declared once in `sys_core/settings_schema.h` with type, default and range.
- Generated code (`components/proto/`) is never hand-edited.
- C++: no exceptions, no RTTI, no `std::thread`; use FreeRTOS primitives through `sys_core` wrappers.

## Android rules
- Kotlin only, Compose only (no XML layouts), Material 3.
- Module boundaries per [docs/07-android-app.md](docs/07-android-app.md); features depend on `core:*`, never on other features.
- All BLE goes through `core:ble`'s `WatchConnection`; UI never touches `BluetoothGatt`.
- State: `ViewModel` exposes `StateFlow<UiState>`; one-off events via `Channel`.
- Every new permission must be justified in `docs/07-android-app.md` permission table (Play policy).

## Protocol rules
- `protocol/proto/*.proto` is the source of truth. Never reuse or renumber a field. Add fields as optional.
- Every new message gets a golden vector in `protocol/testvectors/` used by both the C and Kotlin tests.

## Things that are easy to get wrong on this board (see docs/01-hardware.md)
- CO5300 needs draw areas with even start x/y and odd end x/y → LVGL `rounder` / invalidate-area callback.
- Panel column offset: copy the value from the Waveshare demo for this exact board; do not guess.
- GPIO0, GPIO45, GPIO46 are strapping pins; do not drive them during reset.
- PSRAM is octal (ESP32-S3R8): GPIO33–37 are not available.
- Touch INT (GPIO38) and RTC INT (GPIO39) are not RTC-IO pins: they can wake light sleep, not deep sleep.
- USB-Serial-JTAG and TinyUSB MSC share the USB PHY; switching is a mode change with reconnect.
- The battery is 400 mAh: charge current must be ≤ 200 mA.
