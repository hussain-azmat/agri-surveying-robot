# Architecture

## Two execution contexts

```
 core 1, priority 3, 100 Hz                          Arduino loop() (main), everything slow
┌───────────────────────────────────┐              ┌──────────────────────────────────────────┐
│ control task                      │              │ WiFi / setup portal      wifi_manager    │
│  gps.update   imu.update  battery │   StateLock  │ web server + pages       remote, ui.h    │
│  path follower step (AUTO)        │◄────────────►│ OTA check/flash          ota_github      │
│  safetyEvaluate()  <- ALL rules   │  g_state     │ log rows -> flash -> GitHub  github_log  │
│  motors: drive / stop / ramp      │              │ path save/load/upload    path            │
│  task watchdog (3 s)              │              │ config sync              settings        │
└───────────────────────────────────┘              └──────────────────────────────────────────┘
  never touches flash, WiFi or TLS                     may block for seconds (TLS, flash)
```

The control task is the only code that drives the motors. If the main loop stalls (a slow upload, an OTA flash, a
dead web client), commands stop arriving and the control task stops the rover by itself within the command timeout.
If the control task itself hangs, its watchdog resets the chip, and the motor inputs have pull-downs.

## Modes and who may change them

`DISARMED` (default at every boot and after every OTA) → `MANUAL` (armed from the page) → `AUTO` (follower running).

All transitions go through `safety.cpp` (`stateArm`, `stateDisarm`, `stateStartAuto`) behind one mutex. Entering
`MANUAL`/`AUTO` is refused unless `safetyCanArm` / `safetyCanStartAuto` agree (not tilted, battery ok, GPS fix and a
valid heading for auto). Only the control task's `safetyEvaluate()` decides, every 10 ms, whether anything may move.

## Failsafes (`safety.cpp`, pure and unit-tested)

| Condition | Result |
|---|---|
| MANUAL: no drive command for `cmd_timeout_ms` (500) | stop, stay armed, resume with the next command |
| AUTO: no sign of life from the page for `link_timeout_ms` (3 s) | stop + disarm |
| AUTO: GPS fix lost, stale, or HDOP above `hdop_max` | hold still 1.5 s, then stop + disarm |
| AUTO: heading not valid / IMU lost | stop + disarm |
| AUTO: path finished / off path (several consecutive fixes) / no progress for 30 s / bad path | stop + disarm |
| Tilt beyond 50° for 0.3 s | stop + disarm |
| Battery below critical for 2 s (only if the divider is fitted) | stop + disarm |
| STOP button | stop + disarm, immediately |

A disarm needs a person to arm again. Nothing re-arms itself, and an OTA update never arms the rover.

## Heading without a compass

The MPU6050 has no magnetometer. Heading is gyro-integrated yaw; it is **unaligned** until the rover has driven
straight above 0.5 m/s once, then the GPS course over ground sets it and later courses keep nudging it (so gyro drift
cannot accumulate). Auto mode refuses to start while unaligned. A lost IMU or an I2C recovery marks it unaligned again.

## Path following (`path.cpp`)

Waypoint following with a 4 m acceptance radius (the NEO-6M is ~2.5 m). Hardening found by simulation, and pinned by tests:
* navigation uses the **median of the last 3 fixes**, so one multipath jump moves nothing;
* **at most one waypoint is consumed per new fix**, so a bad sample cannot skip ahead;
* off-path needs **several consecutive** bad fixes, measured against a window of nearby segments, not the whole path
  (so the wrong row of a field is still caught) and not only the current tiny segment;
* it resumes from the nearest segment, so a stopped run can continue.
Steering is a yaw-rate command (proportional to heading error, with gyro-rate feedback); large errors pivot in place.

## Data in the repo

| Path | Written by | Read by |
|---|---|---|
| `ota/version.txt`, `ota/firmware.bin` | CI | rovers |
| `config/rover.cfg` | people | rovers (cached in flash, clamped) |
| `paths/<name>.csv` | rovers (recorded) or people | rovers |
| `logs/<rover>/<day>/<seq>_<n>.csv` | rovers (chunks) | CI (merged nightly) |
| `logs/<rover>/<day>.csv` | CI | people |

## Flash layout (`partitions.csv`, 4 MB)

NVS 20 KB · otadata 8 KB · **app0 1.5 MB** · **app1 1.5 MB** (two OTA slots) · LittleFS 960 KB (log buffer, paths,
cached config). The image is ~1.3 MB, so a slot is ~83% full: `tools/publish_ota.py` refuses an image that no longer fits.
At 5 Hz and ~110 bytes per row, the log buffer holds roughly 30 minutes of driving before the rover must be stopped and
online to upload (logging stops visibly, not silently, if flash fills).

## Tested against

arduino-esp32 core **3.1.3** (IDF 5.3), ArduinoJson **7.4.2**, board `esp32:esp32:esp32`. The firmware stops the build
with a clear error on a 2.x core (different PWM API).
