# UI snapshot tests

Each `<name>.txt` here is a scenario. The simulator runs it headless and the result must match `../ui_snapshots/<name>.png` pixel for pixel.

```bash
cmake -S firmware/simulator -B build/sim && cmake --build build/sim
ctest --test-dir build/sim                       # all scenarios (CI runs this too)
./build/sim/s3w_sim --script firmware/test/ui/gallery.txt --screenshot out.png   # one, by hand
firmware/test/ui/update_snapshots.sh [name...]   # re-render goldens after an intended UI change
```

Review the PNG diffs before committing new goldens. CI uploads the rendered PNGs as an artifact, so a failure can be compared with the goldens.

## Determinism
Headless runs use the timezone `UTC0` and a pinned clock (2026-10-03 10:09 UTC). LVGL time moves only with `wait`, `tap` and `swipe`, in 5 ms steps. Touch and buttons come only from the script. The boot screen fades out for 500 ms before the first command runs.

## Commands
One per line. `#` starts a comment (also after a command).

| Command | Effect |
|---|---|
| `wait <ms>` | let time pass (animations, timers) |
| `tap <x> <y>` | press 80 ms, release, settle 100 ms |
| `hold <x> <y> [ms]` | press ms (default 800, a long-press), release, settle 100 ms |
| `swipe <x1> <y1> <x2> <y2> [ms]` | drag in a straight line (default 200 ms), release, settle 100 ms |
| `key back\|power` | press and release a hardware button (BACK = back, POWER = home, as on the watch) |
| `push <screen-id>` | `ui_nav_push_id()`, e.g. `gallery.lists` |
| `back` / `home` | `ui_nav_back()` / `ui_nav_home()` |
| `toast <text>` | `ui_toast_show()` |
| `clobber` | load a blank screen with auto-delete, as `lcd bars` and the factory test do |
| `start` | `ui_start()`, as console `ui home` does after a clobber |
| `time <HH:MM>` | pin the clock to another time of the same day |
| `time unknown` | "time unknown" state: clocks show `--:--` (svc_time after the RTC lost power) |
| `shot <file.png>` | extra screenshot mid-scenario (the test compares only the final frame) |
| `face <id>` | `wf_set_active()`: `digital`, `analog`, `modular`, `minimal`, the samples `s3w.neon`, `s3w.dial` |
| `aod on\|off` | `wf_set_aod()`: the face's AOD variant |
| `slot <face> <n> <comp\|default>` | `wf_set_slot()`, e.g. `slot minimal 0 moon` |
| `config <cfg>` | `wf_config_load()` with a `FACE_CONFIG` string; fails on a skipped item |
| `config-is <cfg\|->` | fail unless `wf_config_save()` gives cfg (`-` = empty): what the watch would save |
| `face-is <id>` | fail unless id is the active face |
| `data demo\|clear` | the simulator's demo complication data (set at boot) / everything unknown |
| `lit <max-%>` | fail unless fewer than max-% of the pixels are non-black (AOD check) |
| `launcher grid\|list` | `shell_launcher_set_grid()`: the `LAUNCHER_GRID` setting |
| `clock 12\|24` | `ui_clock_set_24h()`: the `TIME_24H` setting |
| `alarm <HH:MM> [once\|daily\|weekdays\|weekends] [label]` | add an alarm to the simulator's clock backend (`sim_clock.c`; demo: 07:00 daily, 08:30 weekends off) |
| `alarm clear` | remove every alarm |
| `timer <seconds>` | start a countdown; it runs on the LVGL tick (`wait` moves it) and rings when done |
| `ring alarm\|timer` | ring the first enabled alarm / finish and ring the first running timer |
| `mode dnd\|sleep\|theater on\|off` | turn a mode on/off by hand (`sim_modes.c`, as quick settings does) |
| `sched dnd\|sleep <HH:MM> <HH:MM> [daily\|weekdays\|weekends]` / `sched dnd\|sleep off` | schedule a mode (default daily) / no schedule |
| `battery <pct>` | fake battery level (`sim_battery.c`; USB power unchanged; demo: 80 %, charged 06:00-08:00) |
| `charger in\|out` | plug / unplug USB power: PMU events, the charging screen opens / closes |
| `low 15\|10\|3` | battery at that level and its low-battery flow (toast / saver alert / watch-only alert) |
| `watchonly` | the WATCH-ONLY screen for the pinned minute (with its burn-in offset) |
| `setting <nvs-key> <value>` | set a Settings app value in the simulator's in-memory store (`sim_settings.c`), e.g. `setting disp_aod 1`, `setting tz JST-9`; fails on an unknown key or an invalid value |
| `setting-is <nvs-key> <value>` | fail unless the setting equals value |
| `world <ids\|->` | `clock_apps_set_world()`: the `WORLD_CLOCKS` setting (`-` = none; demo: tokyo,london,new_york) |

Wait at least 400 ms after a navigation (200 ms slide) and 300 ms after an overlay appears before the scenario ends.

Tap and swipe coordinates depend on the layout. If a layout change moves a target, fix the coordinates and re-render.

## Scenarios
- `home`, `gallery`, `gallery_*`: every built-in screen and widget page. `home` is the default face (Digital Bold).
- `face_<id>`, `face_<id>_aod`: each native face and its AOD variant; the AOD scenarios also check `lit 10` (docs/03 F1: AOD < 10 % lit pixels). AOD goldens include the burn-in offset of the pinned minute (10:09 → +4, +2 px).
- `face_digital_aod_shift`: the minute changes to 10:21 while in AOD; the face moves to that minute's burn-in offset (+2, −4 px).
- `face_neon`, `face_dial` (+ `_aod`, `face_dial_time_unknown`): the sample declarative faces (`watchfaces/samples/*/face.json`), rendered through the face.json loader.
- `face_picker` (long-press → picker at the active face), `face_picker_slot_hold` (long-press on a complication opens the picker, not the app; golden equals `face_picker.png`), `face_picker_swipe` (swipes page through faces; a swipe past the first face is not a tap), `face_picker_select` (tap a preview → home shows it), `face_picker_back` (BACK changes nothing; golden equals `home.png`).
- `face_customize`, `face_choose_slot`, `face_customize_slot`, `face_customize_color`: customize list, the complication chooser, a slot and a colour change (each checks the saved string with `config-is`), `face_config` (a `FACE_CONFIG` string applied as at boot).
- `face_no_data` (all complications unknown), `face_tap` (complication tap → toast while its app does not exist), `face_slot` (a slot set to another complication), `face_analog_time_unknown`.
- `overlay_*`, `dialog`: toast, banner, full-screen alert (BACK must not dismiss it), alert result, confirm dialog.
- `nav_tap`: navigation by touch (a gallery row).
- `nav_edge_back`: a left-edge swipe pops a screen. Its golden equals `gallery.png`.
- `nav_key_home`: BACK pops one screen and POWER goes home. Its golden equals `home.png`.
- `nav_toggle`: scroll a list, then tap a toggle row.
- `nav_restart`: another module deletes the navigation's screen, then `ui_start()` brings home back. Its golden equals `home.png`.
- `home_time_unknown`: home screen while the time is unknown (`--` digits and the "Time not set" hint).
- `power_menu`, `power_menu_confirm`: the power menu (PWR held 2 s on the watch) and its power-off confirmation.
- `shell_qs`, `shell_notifications`, `shell_tiles`, `shell_launcher`: the four swipes on the face (down, up, left, right) open quick settings, notifications, tiles and the launcher. `shell_*_close`: the reverse swipe closes each one; goldens equal `home.png`.
- `shell_qs_toggle` (DND and AOD on, toast), `shell_qs_unavailable` (Wi-Fi: "not available yet"), `shell_qs_action` (Settings: "no app yet").
- `shell_qs_modes` (DND on by its 09:00–11:00 schedule, sleep on by hand, theater tapped on), `shell_qs_modes_skip` (DND turned off inside its window stays off when quick settings opens again).
- `shell_tiles_swipe` (paging both ways, ends on Media), `shell_tiles_last` (no page after Heart rate), `shell_tiles_tap` (a tap opens the tile's app).
- `shell_launcher_key` (BOOT on the face opens the launcher; golden equals `shell_launcher.png`), `shell_launcher_tap` (a row opens its app), `shell_launcher_recent` (an app opened from the launcher shows under Recent), `shell_launcher_switch` (the button at the end switches to the grid), `shell_launcher_grid` (the honeycomb grid).
- `shell_aod_no_swipe`: no panels in AOD; golden equals `face_digital_aod.png`.
- `clock_alarms`, `clock_alarms_empty`, `clock_alarms_12h`: the alarm list (demo alarms, none, 12 h with a labelled third alarm). `shell_launcher_tap` opens Alarms from the launcher; its golden equals `clock_alarms.png`.
- `clock_alarm_toggle` (the switch turns 08:30 on), `clock_alarm_edit`, `clock_alarm_new` (starts at the next full hour), `clock_alarm_days` (untick the weekend, save: Mon-Fri), `clock_alarm_delete` (confirmed).
- `clock_ring_alarm`, `clock_ring_label_12h`: the ring screen. `clock_ring_snooze` (toast), `clock_ring_power` (PWR snoozes; equals `clock_ring_snooze.png`), `clock_ring_slide` (dragging the knob stops it; equals `home.png`), `clock_ring_tap` and `clock_ring_back` (a tap on the slider and BACK do nothing; equal `clock_ring_alarm.png`).
- `clock_ring_timer` (a finished timer rings by itself), `clock_ring_timer_stop` (Stop removes it), `clock_ring_timer_restart`.
- `clock_timer`, `clock_timer_running` (two timers, one paused), `clock_timer_preset`, `clock_timer_custom`.
- `clock_stopwatch` (running, two laps), `clock_stopwatch_stopped` (Reset button), `clock_stopwatch_reset`.
- `clock_world`, `clock_world_empty`, `clock_world_add`, `clock_world_added` (Honolulu appended), `clock_world_remove` (Tokyo removed).
- `clock_face_timer`: a running timer feeds the timer complication (10 min).
- `battery_app`, `battery_app_bottom`: the Battery app (demo: 80 %, about 8 h left, charged 06:00–08:00 in the graph) and scrolled down (saver, watch only, voltage). `battery_app_saver` (the switch turns saver on; the state line follows), `battery_app_watch_only` (the confirmation).
- `battery_charging` (plugged in at 42 %: the charging screen slides up), `battery_charged` (plugged in at 100 %), `battery_charging_tap` (a tap goes to the face), `battery_charging_unplug` (unplugging closes it).
- `battery_low_15` (toast), `battery_low_10` (saver offer), `battery_low_10_accept` (Turn on: quick settings shows saver on), `battery_low_3` (watch-only alert).
- `settings`, `settings_scroll`: the Settings top page and the rest of it (swipe up). `settings_display`, `settings_toggle` (AOD row tapped, `setting-is`), `settings_slider` (Brightness to its 5 % minimum), `settings_choice` (Screen timeout options, tick on 10 s), `settings_choice_pick` (30 s saved), `settings_choice_back` (BACK changes nothing), `settings_scroll_back` (back from a sub page keeps the scroll; golden equals `settings_scroll.png`).
- `settings_dnd` (Do not disturb days = Weekdays), `settings_time`, `settings_time_save` (From 21:00 = 1260), `settings_time_12h` (times as 10:00 PM), `settings_region`, `settings_zone`, `settings_zone_pick` (Honolulu = `HST10`).
- `settings_system`, `settings_about`, `settings_restart`, `settings_restart_cancel`, `settings_factory_dialog`, `settings_factory` (confirmed: AOD back to off), `settings_forget` (no phone link yet: toast), `settings_soon` (Health), `settings_watchfaces` (opens the face picker), `settings_battery` (saver switch), `settings_battery_app` (Battery details opens the Battery app).
- `power_watch_only`, `power_watch_only_12h`: the WATCH-ONLY screen (also checks `lit 3`).
