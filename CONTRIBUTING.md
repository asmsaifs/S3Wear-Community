# Contributing to S3Wear Community

Thanks for helping. This repository is the **Community edition** of S3Wear, a smartwatch firmware for the Waveshare ESP32-S3-Touch-AMOLED-2.06. It is exported from a private source repository, one commit per release.

## What happens to your pull request
- Accepted changes are applied to the private source and appear here in the next release export. The pull request itself is closed with a note, and your authorship is kept in the release commit message (`Co-authored-by`).
- Code you contribute may also ship in the paid Pro edition. That is why a [CLA](CLA.md) is required before the first pull request is merged.

## Before you start
1. Open an issue for anything bigger than a small fix, so you do not build something we cannot take.
2. Read the docs in `docs/` for your area. Key rules: pin numbers only in the BSP, the new I2C driver only, LVGL touched only from the UI task, no blocking in the UI task.
3. Do not add features that exist only in Pro (installable mini apps and games and their SDK, notifications, Find phone, Weather, Media, Calendar, Calls, Home Assistant, voice memos, Wi-Fi). They will not be accepted here.

## Build and test
```bash
# Firmware (ESP-IDF v5.5.x exported)
cd firmware && idf.py set-target esp32s3 && idf.py -B build-community -D SDKCONFIG=build-community/sdkconfig \
    -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.community" build

# Simulator UI snapshot tests
cmake -S firmware/simulator -B build/sim -DS3W_EDITION_PRO=OFF && cmake --build build/sim && ctest --test-dir build/sim

# Host unit tests
cmake -S firmware/host_test -B build/host_test && cmake --build build/host_test && ctest --test-dir build/host_test
```
Keep changes small, with one purpose per pull request. A UI change needs updated goldens (`firmware/test/ui/update_snapshots.sh --community`); look at the PNG diff before you commit it.

## Licence of contributions
Code is Apache-2.0 (`LICENSE`), documentation CC-BY-4.0 (`LICENSE-DOCS`). By signing the CLA you keep your copyright and grant the project the rights it needs to ship your work in both editions.

## Security
Do not open a public issue for a vulnerability. Email the maintainer (see the profile of the repository owner).
