# UI snapshot tests

Each `<name>.txt` here is a scenario. The simulator runs it headless and the result must match `../ui_snapshots/<name>.png` pixel for pixel.

```bash
cmake -S firmware/simulator -B build/sim && cmake --build build/sim
ctest --test-dir build/sim                       # all scenarios (CI runs this too)
./build/sim/s3w_sim --script firmware/test/ui/gallery.txt --screenshot out.png   # one, by hand
firmware/test/ui/update_snapshots.sh [name...]   # re-render goldens after an intended UI change
```

Review the PNG diffs before committing new goldens. CI uploads the rendered PNGs as an artifact, so a failure can be compared with the goldens.

The Community edition (docs/10 §3) has its own scenarios in `community/` and goldens in `../ui_snapshots/community/`, run by a simulator built without the Pro features. The Pro-only commands (notif, find, flash, media, weather, calendar, call, ha, memo, pro) do not exist there.

```bash
cmake -S firmware/simulator -B build/sim-community -DS3W_EDITION_PRO=OFF && cmake --build build/sim-community
ctest --test-dir build/sim-community
firmware/test/ui/update_snapshots.sh --community [name...]
```

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
| `pro locked\|unlocked` | `shell_set_pro_locked()`: no valid Pro licence (svc_license, P12-02); Pro screens open the unlock prompt |
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
| `pair <code> [replace]` | a phone asks to pair: `phone_apps_pair_request()` (the code alert; `replace`: another phone is paired) |
| `pair-end ok\|fail` | the pairing ended: `phone_apps_pair_done()` |
| `pair-is yes\|no\|none` | fail unless the answer to the last request is that (`none`: not answered) |
| `notif <nid> <app> <age-min> <title> \| <text>` | a notification from the phone (`sim_notify.c`): stored, list refreshed, banner; app `whatsapp`, `gmail`, `calendar` (disc icons in their colour, two or one actions) or `news` (no icon) |
| `notif-silent <nid> <app> <age-min> <title> \| <text>` | the same as a silent update: no banner, stays in its place |
| `notif-bitmap <nid> <lines>` | give the notification a phone-rendered text bitmap (a pattern of text-like bars, title plus lines) in 45-row strips |
| `notif-rm <nid>` / `notif-clear` | the phone removed it / everything |
| `notif-done ok\|fail` | the phone's answer to the last action (`fail`: a toast) |
| `notif-sent <what>` | fail unless the UI's last request to the phone was that: `none`, `action <nid> <id>`, `dismiss <nid>`, `clear <count>` |
| `notif-count <n>` | fail unless n notifications are stored |
| `find-watch on\|off` | the phone starts / stops the watch ringing (`sim_find.c`, `find_apps_watch_ring()`) |
| `find-phone idle\|ringing\|stopped\|offline\|noanswer\|unsupported\|refused` | the phone's answer to Find phone (`find_apps_phone()`) |
| `find-asked ring\|stop\|none` | fail unless the app's last request to the phone was that |
| `find-stopped yes\|no` | fail unless the user stopped the watch ringing (or not) |
| `flash-boost yes\|no` | fail unless the Flashlight has the panel at full brightness (or not): the backend `flashlight_backend_t.boost` |
| `flash-keeps-on yes\|no` | fail unless the top screen holds the screen on (`UI_SCREEN_KEEP_ON`, or not) |
| `media none\|offline\|playing\|paused\|long\|nonlatin\|live\|novol` | the phone's media session (`sim_media.c`; default: connected, nothing playing): the demo track (Spotify, "Blue in Green", 1:23 of 5:37, volume 9/15) playing or paused, too long for one line, a long Bangla title (glyphs the fonts lack), a live stream (no duration), a player without volume control |
| `media-art demo\|none\|broken` | the artwork arrives: the demo 120 × 120 JPEG decoded by `media_art.c`, none, or a truncated JPEG that must fail to decode |
| `media-text on\|off` | the phone draws titles the fonts lack: the `nonlatin` session gets a title bitmap (bars stand in for the text) |
| `media-asked <cmd>\|none` | fail unless the Media app's last command was that: `play`, `pause`, `next`, `prev`, `vol+`, `vol-` |
| `media-fail offline\|noanswer\|unsupported\|refused` | a media command failed (`media_apps_command_failed()`: a toast) |
| `weather demo\|stale\|expired\|current\|night\|none` | the forecast from the phone (`sim_weather.c`; default: none, so faces keep the `data demo` weather): Reykjavík fetched 9 min ago (partly cloudy 6°, 24 hours, 7 days, UV, AQI), the same fetched 4 h ago (stale), 30 h ago (no temperature for now), current conditions only, a clear night below zero, none |
| `calendar demo\|empty\|none` | the agenda from the phone (`sim_calendar.c`; default: none, so faces keep the `data demo` event): synced 2 min ago, today's standup (going on at 10:09), design review, lunch, dentist, tomorrow's all-day holiday, a flight, a too-long workshop title and location, Monday's planning; an empty agenda; none |
| `call ring <name>\|<number>` | an incoming call from the phone (`sim_call.c`, `call_apps_set()`; a new call id each time; either part may be empty) |
| `call ring-nocontrol <name>\|<number>` | the same, without call control on the phone |
| `call answered` | the phone answered the ringing call (1 min 5 s ago, without name and number, as the phone sends it) |
| `call active <name>\|<number>` | a call dialled on the phone, on for 12 min 34 s |
| `call end\|missed` | the call ended / was missed |
| `call missed-demo` | three missed calls: 25 min ago, yesterday, 8 days ago |
| `call-asked answer\|decline\|silence\|end\|clear\|silenced\|none` | fail unless the screens' last request was that (`silenced`: BACK stopped the watch ring only) |
| `call-fail offline\|noanswer\|unsupported\|denied\|refused` | the last call command failed (`call_apps_result()`: a toast) |
| `link connected\|reconnecting\|unpaired` | the phone link state on Settings > Connections (`sim_connect.c`; default connected) |
| `link-sync <seconds>\|never` | age of the last sync with the phone (default 300) |
| `link-bt-is on\|off` | fail unless the Bluetooth switch is that |
| `link-asked reconnect\|forget\|none` | fail unless the Connections page's last request was that |
| `wifi off\|searching\|joining\|joined\|notfound\|auth\|noip` | Wi-Fi state on Settings > Connections > Wi-Fi (the network: the first saved one) |
| `wifi-saved <ssid>[,<ssid>...]\|none` | the saved Wi-Fi networks (default `Home`) |
| `wifi-is on\|off` | fail unless the Wi-Fi switch is that |
| `wifi-forgot <ssid>\|none` | fail unless the last network forgotten on the Wi-Fi page was that |
| `ha demo\|unread\|empty\|none` | Home Assistant (`sim_ha.c`): six entities read over Wi-Fi (Kitchen on, Living room lamp off, Movie night, Temperature 21.5 °C, Coffee maker unavailable, Front door) / the same never read / set up without entities / not set up |
| `ha-via wifi\|phone`, `ha-error <offline\|unreachable\|auth\|refused\|noanswer\|unsupported\|none>`, `ha-busy <n>`, `ha-refreshing` | how the last call went: the path, the error, a toggle on its way, a refresh running |
| `ha-asked tap <n>\|refresh\|none` | fail unless the Home screens' last request was that (a tap answers at once: a toggle flips) |
| `memo demo\|none` | voice memos (`sim_memo.c`): three (Oct 3 09:55 0:42 being sent, Oct 2 18:30 1:15 on the phone, Oct 1 07:18 0:08) / none |
| `memo-state idle\|rec <s> <level>\|play <n> <s>` | the recorder (s seconds, mic level 0..100, of 5:00) / player (memo n, 0 = newest, at s seconds) state |
| `memo-result saved\|full\|short\|nospace\|mic\|storage\|played\|playfail` | how a recording or playback ended (`memo_apps_result()`: the toast, the recorder closes) |
| `memo-asked record\|stop\|play <n>\|delete <n>\|none` | fail unless the memo app's last request was that (n: the memo's list index) |
| `app <name\|id\|file.wasm>` | run a mini app (`sim_app.c`): a preinstalled app (`apps/<name>`: its folder name or id, with its manifest), an installed app's id (with its manifest), an SDK sample (`widgets`, `bench`, `iodemo`: `sdk/examples/<name>`; `iodemo` gets the watch's grants), a test app from `components/app_runtime/test_apps` (`trap`, `spin`, `hello`, ...) or a `.wasm` path |
| `app-install <file.s3app>` | check a package (path relative to `components/app_runtime/test_apps`, e.g. `pkg/hello.s3app`; the test key is trusted, unsigned is allowed) and show the consent screen (Install installs); a refused package toasts "App not installed". Every run starts with no apps installed (a temporary root) |
| `app-uninstall <id>` | uninstall at once (no dialog) |
| `app-installed <id> <version\|none>` | fail unless that version (or none) is installed |
| `app-stop` | the system stops the app (`s3w_on_stop`) |
| `imu <x> <y> <z>` | the accelerometer (mg, watch frame) apps read (default 0 0 1000: lying flat) |
| `app-http <status> [body]\|error` | the phone's answer to the app's waiting HTTP request (`error`: a network error) |
| `app-frames <n>` | fail unless the running app has run n frames: a canvas app's `s3w_on_frame` calls, a widgets app's 30 fps frame periods of run time (P8-07) |
| `app-is running\|exited\|trapped\|hung\|none` | fail unless the current (or last) app is in that state |
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
- `shell_qs_toggle` (DND and AOD on, toast), `shell_qs_unavailable` (Wi-Fi: "not available yet"), `shell_qs_bluetooth` (Bluetooth off: toast, the Connections switch follows), `shell_qs_action` (Settings: "no app yet").
- `shell_qs_modes` (DND on by its 09:00–11:00 schedule, sleep on by hand, theater tapped on), `shell_qs_modes_skip` (DND turned off inside its window stays off when quick settings opens again).
- `shell_tiles_swipe` (paging both ways, ends on Media), `shell_tiles_last` (no page after Heart rate), `shell_tiles_tap` (a tap opens the tile's app).
- `shell_launcher_key` (BOOT on the face opens the launcher; golden equals `shell_launcher.png`), `shell_launcher_tap` (a row opens its app), `shell_launcher_recent` (an app opened from the launcher shows under Recent; Snake since P8-08), `shell_launcher_switch` (the button at the end switches to the grid), `shell_launcher_grid` (the honeycomb grid).
- `shell_aod_no_swipe`: no panels in AOD; golden equals `face_digital_aod.png`.
- `clock_alarms`, `clock_alarms_empty`, `clock_alarms_12h`: the alarm list (demo alarms, none, 12 h with a labelled third alarm). `shell_launcher_tap` opens Alarms from the launcher; its golden equals `clock_alarms.png`.
- `clock_alarm_toggle` (the switch turns 08:30 on), `clock_alarm_edit`, `clock_alarm_new` (starts at the next full hour), `clock_alarm_days` (untick the weekend, save: Mon-Fri), `clock_alarm_delete` (confirmed).
- `clock_ring_alarm`, `clock_ring_label_12h`: the ring screen. `clock_ring_label_long`: a label longer than one line stays on one line ending in "...", clear of the time. `clock_ring_snooze` (toast), `clock_ring_power` (PWR snoozes; equals `clock_ring_snooze.png`), `clock_ring_slide` (dragging the knob stops it; equals `home.png`), `clock_ring_tap` and `clock_ring_back` (a tap on the slider and BACK do nothing; equal `clock_ring_alarm.png`).
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
- `settings_system`, `settings_about`, `settings_restart`, `settings_restart_cancel`, `settings_factory_dialog`, `settings_factory` (confirmed: AOD back to off), `settings_connections` (opens the Connections page), `settings_soon` (Health), `settings_watchfaces` (opens the face picker), `settings_battery` (saver switch), `settings_battery_app` (Battery details opens the Battery app).
- `connections` (connected, synced 5 min ago), `connections_reconnecting` (Reconnect row, 2 h ago), `connections_never_synced`, `connections_unpaired` (no sync / Reconnect / Forget rows), `connections_off` and `connections_on` (the Bluetooth switch), `connections_reconnect` (toast, request recorded), `connections_wifi` (soon), `connections_forget_dialog`, `connections_forget` (confirmed: not paired).
- `ble_pair` (the code alert), `ble_pair_replace` (another phone is paired; leading zeros), `ble_pair_accept` (Pair → answer yes, "Phone paired" toast), `ble_pair_cancel` (BACK → answer no, no toast; golden equals `home.png`), `ble_pair_timeout` (the phone gave up: the alert closes, "Pairing failed").
- `find_phone` (idle), `find_phone_asking` (tap: asked to ring), `find_phone_ringing`, `find_phone_stop` (tap while ringing: stop asked; equals `find_phone.png`), `find_phone_stopped` (the phone stopped by itself; equals `find_phone.png`), `find_phone_offline`, `find_phone_unsupported` (the reason in red), `find_phone_reopen` (leave while ringing and reopen; equals `find_phone_ringing.png`).
- `find_watch` (white phase), `find_watch_flash` (500 ms later, black), `find_watch_stop`, `find_watch_tap`, `find_watch_key`, `find_watch_back` (Stop, a tap anywhere, PWR and BACK stop it and tell the backend; equal `home.png`), `find_watch_remote_stop` (the phone stops it; equals `home.png`), `find_watch_over_app` (it closes back to the open app; equals `find_phone.png`).
- `media_none`, `media_offline` (no controls), `media_playing` (the note until the artwork comes), `media_artwork` (the demo JPEG decoded), `media_artwork_late` (it arrives while open; equals `media_artwork.png`), `media_artwork_broken` (a JPEG that fails keeps the note; equals `media_playing.png`), `media_paused`, `media_progress` (10 s later: 1:33), `media_long` (one line each, "…"), `media_live` (no progress bar), `media_novol` (no volume row), `media_disconnect` (the phone goes away while open; equals `media_offline.png`), `media_nonlatin` and `media_nonlatin_next` (a long Bangla title, also replacing a long Latin one while open: the case that hung the watch's draw task with LVGL's DOTS mode; boxes without a phone-drawn title), `media_text_bitmap`, `media_text_bitmap_late` (the picture replaces the labels, also when it arrives while open), `media_tile_text_bitmap`.
- `media_pause_tap`, `media_play_tap`, `media_next`, `media_prev`, `media_volume_up`, `media_volume_down` (each checks the command with `media-asked`; play / pause and volume show at once), `media_fail` (the toast).
- `media_tile`, `media_tile_long`, `media_tile_offline` (the Media tile), `media_tile_tap` (a tap opens the app).
- `weather_app` (the hero, details, hours), `weather_days` (scrolled: the days and sunrise / sunset), `weather_hours_scroll` (the hours row scrolled sideways, 12 h clock), `weather_none`, `weather_stale` (the hour from the list, "Updated 4 h ago" in the warning colour), `weather_expired` ("--", the days from today), `weather_current` (no place, hours or days), `weather_night` (moon icons, below zero), `weather_minute` (two hours later on the open app), `weather_tile`, `weather_tile_stale` (dimmed), `weather_tile_tap` (opens the app), `weather_face` (Modular: the weather circle and the sunset pill from the forecast), `weather_face_night` (Minimal's weather pill).
- `pro_locked_app` (a Pro screen pushed while not licensed opens the unlock prompt instead), `pro_locked_swipe` (the swipe up to notifications; golden equals `pro_locked_app.png`), `pro_locked_tile` (the Weather tile's tap; same golden), `pro_locked_free_app` (Alarms still opens; golden equals `clock_alarms_empty.png`), `pro_unlocked` (locked then unlocked: Weather opens; golden equals `weather_none.png`) — P12-02.
- `call_incoming`, `call_incoming_number` (no name: the number, a phone for the initial), `call_incoming_unknown` (withheld, no call control: "Answer on your phone"), `call_mute` and `call_mute_key` (Mute and PWR: "Muted", silence asked), `call_answer`, `call_decline` (asked; the screen waits for the phone), `call_answered` (the call screen, 1:05), `call_end` (asked), `call_ended` (equals `home.png`), `call_back` (BACK: the watch ring stops; equals `home.png`), `call_missed` (the banner), `call_over_app` (closes back to the open app), `call_fail_denied` (the toast), `calls_app_empty`, `calls_app_missed` (today, Yesterday, a date), `calls_app_oncall` (a dialled call on top), `calls_app_open_call` (tapping it opens the call screen).
- `memos_list` (the record button, three memos: sending, on the phone, neither), `memos_empty`, `memos_record` (the button asks to record and opens the recorder: 0:12, level, of 5:00), `memos_record_stop` (Stop: asked, back to the list, "Memo saved"), `memos_record_back` (BACK stops too), `memos_record_full` (it ended by itself: the recorder closes, "limit reached"), `memos_record_nospace` (could not start: closes with the reason), `memos_player` (a memo on the phone), `memos_play` (Play asked; ring and Stop while it plays), `memos_play_end` (back to Play), `memos_delete_dialog`, `memos_delete` (confirmed: asked, the player closes).
- `calendar_app` (today: "Now", colours, locations), `calendar_scrolled` (tomorrow, all day, "…", Monday, "Updated"), `calendar_later` (30 min later on the open app, 12 h clock), `calendar_empty`, `calendar_none`, `calendar_tile`, `calendar_tile_empty`, `calendar_tile_tap` (opens the agenda), `calendar_face` (Modular's next-event pill).
- `power_watch_only`, `power_watch_only_12h`: the WATCH-ONLY screen (also checks `lit 3`).
- `app_widgets` (the SDK widgets sample in WAMR: clock from `s3w_time_local`, ring, button, slider, switch), `app_widgets_events` (+10 twice, the switch starts a 1 s app timer: 60 %, "Elapsed 2 s"), `app_widgets_slider` (dragging sends VALUE events: 95 %), `app_widgets_back` (BACK stops the app; equals `home.png`), `app_trap` ("App stopped"), `app_hang` (`s3w_on_start` never returns: the watchdog kills it, "App not responding"). `app_canvas_bench` (P8-03: the canvas benchmark after 2.2 s: 48 sprites, spinning line, ring, "60 fps"), `app_gfx_mode` (a widgets app calling `s3w_gfx_clear` traps: "App stopped"). `app_iodemo` (P8-04: tilt rolls the ball right, two taps leave dots and count, a swipe up's HTTP answer: "HTTP 200, 11 bytes"), `app_iodemo_hold` (a long press posts a notification, a swipe down clears the count).
- P8-07, the preinstalled apps (each ends with `app-frames 300` and `app-is running`, or shows a second screen): `app_calculator` (12.5 × 4 = 50), `app_calculator_history` (two results, newest first), `app_converter` (26 km in miles), `app_converter_units` (the unit picker), `app_pomodoro` (started: 24:50, Pause / Skip), `app_pomodoro_skip` (short break waiting for Start), `app_breathe` (10.5 s in: breathing in, 0:50 left), `app_breathe_done` (a whole minute: "Well done", 1 min today), `app_dice` (a tap, three dice, a shake rolls them; 75 s for 300 frames at the idle 4 fps), `app_dice_coin`, `app_tip` (42.50, 20 %, 3 people: 17.00 each), `app_metronome` (101 BPM playing), `app_tally` (A 5, B 1), `app_tally_reset` (the confirm dialog), `app_level` (5° tilt: bubble right), `app_level_calibrate` (tap: level, green), `app_habits` (two of four ticked), `app_habits_add` (the presets), `app_preinstalled_launcher` (the Mini apps section), `app_preinstalled_open` (a tap runs Calculator).
- P8-08, the preinstalled games (each with `app-frames 300` and `app-is running`): `app_2048` (five swipes), `app_snake` (a turn down into the wall: game over), `app_blocks` (pieces dragged, turned, dropped), `app_flappy` (five flaps, mid-flight), `app_flappy_over` (no flaps: game over), `app_maze` (a random maze, tilted: the ball rolls), `app_breakout` (launched, bricks broken), `app_minesweeper` (the first tap opens an area, a flag), `app_tictactoe` (the watch blocks and wins), `app_simon` (round 2), `app_reaction` (835 ms, a new best), `app_highscore_persist` (the app stopped and run again: its title shows the best), `app_shooter` (a wave, held fire), `app_games_launcher` (the Games section).

## Files app scenarios (P8-18)
`app_files_*.txt` install `components/app_runtime/test_apps/pkg/files.s3app` (a build of `store/apps/files`; rebuild it with `pkg/make.sh` and re-render the goldens after changing that app) and run it. The simulator seeds the device-file roots (`/flash`, `/sd`) from `firmware/test/ui/files/{flash,sd}` into its temporary directory at the first app start, so deleting in a scenario never touches the repo. Row taps assume the name-sorted listing of those fixtures.
