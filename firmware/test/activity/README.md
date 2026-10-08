# Accelerometer logs for the step detector

`test_step_detect` (firmware/host_test) runs `step_detect.c` over every `*.csv` here and
checks the counted steps against the true count.

## Format

```
# s3w accel log v1: t_ms,x,y,z (mg, watch frame)
# source=...
# steps=1000      <- the true step count (required)
# tol=20          <- allowed error in steps (optional; default 7 % of steps)
t_ms,x,y,z
0,-985,17,1
47,-1009,-13,0
```

`t_ms` is the sample time in ms (any rate; the watch's FIFO gives 21 Hz, 62.5 Hz during raise
windows). `x,y,z` are mg in the watch frame (+x 3 o'clock, +y 12 o'clock, +z out of the screen).
Lines starting with `#` are comments; `# watch_steps=N` (what the watch counted) is ignored.

## Recording on the watch

1. SD card in, watch on the wrist. On the console: `imu log walk_1000 900` (name, seconds;
   or `imu log <name>` and later `imu log stop`). The watch keeps counting as usual; the
   file is `/sd/imu/<name>.csv`.
2. Do the activity and count the steps (a hand tally counter helps). Note the console
   `activity` steps before and after to compare.
3. Copy the file here (card reader, or over the console with `fs cat <file> <offset>`,
   4 KB per call), replace `# steps=?` with your
   count, add `# tol=N` for logs that must stay near 0 (desk, typing, driving).
4. `ctest --test-dir build/host_test -R StepDetect` — the test prints each log's result.

## Files

- `watch_*.csv`: recorded on the watch.
- `synth_*.csv`: synthetic (`tools/activity/gen_synth.py`, a swinging-arm model with jitter
  and noise; fixed seeds). They exercise the run logic, rates and the negative cases but are
  not a substitute for real recordings: the ±7 % acceptance (P5-01) is the 1000-step walk on
  the wrist.
