# 08 — Quality, CI/CD, release, provisioning

## 1. Test pyramid
| Level | Firmware | Android | Runs in |
|---|---|---|---|
| Unit (host) | GoogleTest for pure logic: alarm scheduler, raise detector, time zones, face JSON parser, protobuf golden vectors, settings schema | JUnit5 + MockK + Turbine | CI every push |
| Component | Simulator scripted scenarios → PNG snapshot diff (tolerance 0.5 %) for every screen | Robolectric + FakeWatch integration; Roborazzi screenshots | CI every push |
| On-target | Unity tests via `pytest-embedded` on a USB-connected board (self-hosted runner): I2C devices respond, display init, audio loopback (speaker→mic tone detection), IMU sanity, RTC tick, SD r/w, BLE advertising | Instrumented tests on emulator (non-BLE) | nightly + before release |
| System | Manual test plan (checklist per release): 7-day battery test, alarm from deep sleep, every screen | same, on device matrix | release candidate |
| Soak | 72 h continuous: random UI monkey (simulated touch script on device via console) | | release candidate |

Coverage gate: ≥ 70 % lines on host-tested firmware logic, ≥ 70 % on Android `core:*` and `domain`.

## 2. Static analysis
Firmware: `-Wall -Wextra -Werror` on our components, clang-tidy (bugprone, cert, performance), cppcheck, `idf.py size-components` report with budget (fail if app > 5 MB or internal DRAM static use grows > 5 % without label `size-ok`). Android: ktlint, detekt, Android Lint (fatal on errors). Both: gitleaks secret scanning, dependency licence check.

## 3. CI workflows (`.github/workflows/`)
| Workflow | Trigger | Steps |
|---|---|---|
| `firmware.yml` | push/PR touching `firmware/`, `protocol/` | `espressif/idf:v5.5.x` container → build debug + release → size report → host tests → simulator snapshot tests → artifacts (`.bin`, `.elf`, `.map`) |
| `android.yml` | push/PR touching `android/`, `protocol/` | JDK 21 → lint/detekt → unit → Roborazzi verify → assembleDebug; on tag: bundleRelease signed via secrets |
| `protocol.yml` | `protocol/` | regenerate code, fail if generated output differs from committed, run golden vectors on both sides |
| `release.yml` | tag `fw-v*` | build release firmware → GitHub Release |
| `hil.yml` | nightly, self-hosted runner with board | flash + pytest-embedded suite |

Current state (P0-07): `firmware.yml`, `android.yml` and `protocol.yml` exist and run on push to `main` and on PRs (path-filtered, Ubuntu runners). The release firmware build switches on automatically once `firmware/sdkconfig.defaults.release` exists; the simulator job runs the UI snapshot tests (`ctest --test-dir build/sim`, P2-05) and uploads the rendered PNGs. Not yet wired: size budget gate, Roborazzi, signed `bundleRelease` on tags, clang-tidy/cppcheck, gitleaks — each lands with the task that needs it. The ESP-IDF container tag (`espressif/idf:v5.5.1`) matches `firmware/dependencies.lock`; bump both together.

## 4. Versioning & branching
- Trunk-based: `main` always releasable; short-lived feature branches; squash merge; Conventional Commits.
- Firmware SemVer `fw-vX.Y.Z`; Android `versionName` X.Y.Z / `versionCode` monotonic; protocol `major.minor` in `protocol/VERSION`.
- Compatibility matrix in `docs/compat.md` (generated): which app versions talk to which firmware.

## 5. Release process
1. Freeze → RC tag → manual checklist + 72 h soak.
2. Publish the firmware binaries on GitHub Releases; users flash over USB (`idf.py flash` or the ESP web flasher).

## 6. Provisioning (per unit, for anyone building more than one watch)
`tools/factory/`:
1. Flash factory image (debug-signed) → run on-device factory test screen (display colour bars, touch grid, buttons, speaker tone + mic loopback, IMU axes, RTC, SD, BLE RSSI, battery read) → result JSON over USB.
   On-device part implemented in P1-09 (`components/factory_test`): console `factory test` (or `factory test auto` to skip the button/tap steps) runs i2c, display (TE), touch (ID), rtc (ticking), pmu (battery + 200 mA charge limit), imu (self-test + 1 g), audio (1 kHz speaker→mic loopback, both mics), flash, sd (SKIP without card), ble (advertise), wifi (scan), btn_boot, btn_pwr, touch_tap, shows a checklist on screen and prints one line:
   `{"factory_test":{"fw":"<version>","pass":true,"skipped":0,"results":[{"name":"i2c","status":"PASS","detail":"6/6"},...]}}`
   `status` is PASS/FAIL/SKIP; `pass` is true when nothing FAILed. `tools/factory/` (host side) parses this line.
2. Write serial number + hw revision to an NVS factory partition / eFuse user block.
3. For release units only: run `provision_release.sh` → generates/flashes secure boot digest and enables flash encryption (irreversible; keys from offline backup).
4. Flash release firmware, verify boot, print label.

