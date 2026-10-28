# Hardware verification sign-off

Each function is verified on the real rover **separately**, in the order below, and signed off here. A function is "verified" only
when every box is ticked **and** the evidence (serial output, photo, short video) is attached to the pull request or stored next to
this file. The full steps for each function are in `Software/<Function>/Test/ON_ROVER_TEST.txt`; the pass criteria are in
`Software/<Function>/Readme.txt`.

**Rules:** wheels off the ground until the checklist says otherwise. Never put a WiFi password, token or access code in the evidence.
Serial monitor at 115200 baud. Where a function has a bench sketch, flash that one (it runs only that function); otherwise use the
full firmware.

| # | Function | How to run it | Verified (date) | By | Evidence |
|---|---|---|---|---|---|
| 1 | Motors | motors bench | | | |
| 2 | MPU | mpu bench | | | |
| 3 | GPS | gps bench (outdoors) | | | |
| 4 | WiFi setup | wifi bench | | | |
| 5 | OTA | ota bench | | | |
| 6 | Remote | full firmware | | | |
| 7 | Safety | full firmware | | | |
| 8 | Logging | full firmware (needs a GitHub token on the rover) | | | |
| 9 | Path | full firmware, outdoors, last | | | |

## 1. Motors (wheels off the ground)
- [ ] EN jumpers removed, 10 k pull-downs on the 4 EN pins, RR enable wire moved to GPIO 16
- [ ] 10 resets with the monitor open: no wheel twitches
- [ ] Keys 1-4 turn FL, FR, RL, RR **forward** for ~1 s, then stop by themselves
- [ ] Keys 5-8 turn the same wheels **backward**
- [ ] Key `s` stops a spinning wheel at once
- [ ] Lowest PWM at which each wheel turns recorded (FL __ FR __ RL __ RR __); `MOTOR_MIN_DUTY` adjusted if needed

## 2. MPU
- [ ] Boot message names the I2C pins and says "MPU6050 Initialized Successfully" (pins confirmed against the real wiring)
- [ ] `ok:true, calibrated:true` within ~5 s, rover still
- [ ] Flat and still: accel_z ≈ 9.8, accel_x/y ≈ 0, gyro ≈ 0, tilt < 2°
- [ ] Tilting the rover moves pitch/roll/tilt the right way (directions written down)
- [ ] `gyro_noise_dps` < 0.5 with the motors off; does not rise sharply with the motors running

## 3. GPS (outdoors, clear sky)
- [ ] `healthy:true`, `baud:38400` within ~10 s
- [ ] `rate_hz` ≈ 5.0
- [ ] Fix acquired: sats ≥ 6, hdop < 3 (time to fix: ____)
- [ ] lat/lon within ~5 m of a phone map at the same spot
- [ ] Walk 20 m: position moves ~20 m, speed and course are plausible
- [ ] Unplug/replug the GPS: it recovers by itself

## 4. WiFi setup
- [ ] `RoverSetup-xxxx` appears; password = code from `python tools/ap_password.py <MAC>`
- [ ] Any web page opens the setup page
- [ ] **Wrong** WiFi password: reported as failed, nothing saved
- [ ] **Right** password: "connected and saved"; after reboot it reconnects by itself
- [ ] Setup button (GPIO 15) held ~5 s at power-up brings the setup page back
- [ ] 4 minutes idle in setup: offline mode, the rover's own network still works
- [ ] No secret appears in the serial output

## 5. OTA
- [ ] Same version in the repo: "up to date"
- [ ] Newer version published: `c` reports "available"
- [ ] `i` installs: `OTA_START`, `OTA_DONE`, reboot into the new version, rover **disarmed**
- [ ] Bad download (wrong MD5 / power cut mid-download): failure reported, old firmware keeps running
- [ ] An older version in the repo is ignored
- [ ] (Full firmware) update refused while armed

## 6. Remote
- [ ] Control page opens on the rover's own network and on the shared network
- [ ] Wrong access code refused on the shared network
- [ ] Arm, joystick drives the wheels, releasing stops them; STOP disarms at once
- [ ] Closing the page / leaving WiFi range stops the wheels within 0.5 s (time: ____)
- [ ] System buttons answer: check update, calibrate IMU, spin wheel

## 7. Safety (wheels off the ground first, then a safe open patch)
- [ ] Armed rover with a silent control page stops within 0.5 s
- [ ] AUTO with the GPS antenna covered: holds, then disarms with a stated reason
- [ ] Tilt past the limit disarms
- [ ] Every boot and every OTA leaves the rover disarmed
- [ ] Known limit noted: no battery failsafe until the battery divider is fitted

## 8. Logging
- [ ] Arm, drive a known loop, disarm, wait ~20 s: a chunk appears under `logs/<rover>/<today>/`
- [ ] The metadata line and all 32 columns present (see `Software/Logging/Readme.txt`); one row per GPS fix (`fix_n` goes up by 1); IMU columns filled; empty where not available
- [ ] Driving straight with no turn button: `drift_deg` and `straight_s` fill in after about a second; a turn button empties them
- [ ] Plotting lat,lon shows the loop that was driven
- [ ] Offline test: rows are kept in flash and uploaded later

## 9. Path (outdoors, last)
- [ ] Record, save and load a ~20 m square
- [ ] AUTO replays it; max and mean `xte_m` and end-point error recorded (max ____ mean ____ end ____)
- [ ] Leaving the path, losing GPS and getting stuck each stop the rover with a reason
- [ ] `turn_full_dps` calibrated and written to `config/rover.cfg`
