# Bring-up checklist

Everything in this repo is tested on a laptop. This checklist is how each stage becomes tested on the **real rover**.
Do the stages in order; each one needs the previous. Record the measurements: they become the baseline.
**Wheels off the ground (rover on a stand) until Stage 3 says otherwise.**

## Stage 0: it builds, motors are off, the IMU is stable

- [ ] Remove the L298N EN jumpers, fit 10 k pull-downs on the 4 EN pins, and **move the RR enable wire from GPIO 21 to GPIO 16**
      (GPIO 21 is the IMU's SDA). Power the rover with the **wheels off the ground**.
- [ ] Flash by USB. Serial console shows `=== agri rover firmware v0.1.0 ===`, the MAC, and `ready, disarmed`.
- [ ] **Motors off at boot:** press reset 10 times with the console open. No wheel twitches at any reset.
- [ ] Open the control page → System → **Spin wheel 1 s** for each of FL, FR, RL, RR. Each must turn the expected wheel,
      *forward* (positive speed). If a wheel is wrong or reversed, fix `PIN_MOTOR_*` / `WHEEL_INVERT` in `config.h` (or swap that motor's wires).
- [ ] Find the lowest PWM at which each wheel actually turns; compare with `MOTOR_MIN_DUTY` (0.18) and adjust.
- [ ] **IMU stable with the motors running:** note *gyro noise* on the control page with the motors off (should be well
      under 0.5 dps), then arm and drive all wheels at 50% and 100%. It should not rise sharply, and the status should
      never show "IMU missing". If it does: shorten/twist the I2C wires, separate them from motor leads, add a 100 nF cap at the sensor.
- [ ] **Calibrate** (System → Calibrate IMU, rover still). Tilt reads ~0° when level.
- [ ] Confirm no secret appears in the serial output at any point (WiFi password, token).

## Stage 1: WiFi setup and OTA

- [ ] Fresh flash with no saved WiFi: the `RoverSetup-xxxx` network appears. Its password is `python tools/ap_password.py <MAC>`.
- [ ] Join, open any page: the setup page appears. Pick the WiFi, enter a **wrong** password: it reports failure and saves nothing.
- [ ] Enter the right one: "Connected and saved". Reboot: it reconnects by itself.
- [ ] **Hold the button for 5 s at power-up:** setup mode returns. **Do nothing for 4 minutes in setup:** offline mode; the
      control page still works on the rover's own network.
- [ ] Add the GitHub token. Push a commit that bumps the version; wait for CI; reboot the rover: as soon as it is online it checks and updates by itself (or press
      *Check for update*, then *Install update*), reboots, shows "Updated from vX to vY" and is **disarmed**.
- [ ] **Bad download keeps the old firmware:** temporarily publish a `version.txt` with a wrong MD5 on a test branch/copy (or
      power-cycle the rover mid-download). The rover must report the failure and keep running the old version.
- [ ] Arm the rover and request an update: it must refuse ("disarm first").

## Stage 2: GPS logged to GitHub

- [ ] Antenna outside the chassis with a clear sky. Control page: satellites rise, a fix appears, **GPS rate ≈ 5.0 Hz**, HDOP < 3 in the open.
- [ ] Arm, push the rover by hand or drive it slowly around a **known loop** (e.g. a 20 m square), disarm.
- [ ] Wait ~20 s: a chunk appears under `logs/<rover>/<today>/`. Check the CSV columns. Run `tools/consolidate_logs.py`
      (or wait for the nightly job) and plot `lat,lon`: the loop must look like the loop you walked.
- [ ] Unplug WiFi, log a run, restore WiFi: the data uploads later; nothing lost.
- [ ] Measure what the GPS really delivers in your field: HDOP, satellites, and drift of a stationary rover over 5 minutes.

## Stage 3: phone control with failsafe

- [ ] From a phone, arm, drive. **Rover on the ground only after the next three checks pass on the stand.**
- [ ] **Link loss:** drive, then turn the phone's WiFi off. The rover must stop within about half a second.
- [ ] **STOP:** works from any state, including mid-turn.
- [ ] **Page hang / close the tab while driving:** the rover stops.
- [ ] Kill the router (online mode): driving over the rover's own network still works after it falls back.
- [ ] Measure the actual stop distance at 100% speed.

## Stage 4: record and save a path

- [ ] Arm, *Start*, drive a route with a few turns, *Stop & save*. A `paths/<name>.csv` appears in the repo within a minute of the rover being stopped and online.
- [ ] The CSV has a sensible number of points (one per ~1.5 m or per turn, thinned), and plots as the route you drove.

## Stage 5: replay with error measurements

- [ ] **Calibrate `turn_full_dps`:** at turn = 1, read the real yaw rate (gyro) and set it in `config/rover.cfg`.
- [ ] Heading aligns: drive ~5 m straight in manual; the control page shows a heading.
- [ ] Place the rover at the start of the recorded path, *Load*, *Start auto*. It follows and stops at the end.
- [ ] **Failsafes in auto:** cover the antenna (no fix → stop), push the rover off the path (off-path stop), close the page (link-loss stop).
- [ ] **Measure:** the log has `xte_m` (cross-track error) and `wp` per row in auto mode. Report max and mean `xte_m`, end-point error, and
      time per lap over at least 5 laps. *(The exact measurements requested in the brief were cut off; confirm which ones are wanted.)*
- [ ] Tune `accept_radius_m`, `auto_speed`, and (if it wanders) the steering gains in `path.h` against those numbers.

## Before leaving it unattended

- [ ] Fit the battery divider, set `BATT_SENSE_ENABLED 1`, check thresholds for your pack.
- [ ] Fit a fuse on the battery output; check the L298N channels under a real stall (EDD s6.6).
