# Agri surveying rover

Firmware and tooling for the 4-wheel skid-steer survey rover (ESP32-WROOM-32, 4 independent motor drivers,
MPU6050, u-blox NEO-6M). Its pins follow the running sketch `surveying_robot.ino` (four L298N modules). It is driven from a phone or laptop web page, or follows a recorded latitude/longitude
path, and everything it does is logged to this repository. Updates arrive over the air from this same repository.

Design source: *Agri Surveying Robot, Engineering Design Document V0.5* (hardware, power, software architecture).

> **Status: v0.1.0, firmware complete, compiles, unit-tested on a laptop. NOT yet run on the real rover.**
> Read [What has and has not been verified](#what-has-and-has-not-been-verified) before the first power-up.

## What it does

* **Remote driving** from a phone: arm, joystick, big STOP button. The rover stops by itself if commands stop for
  about 0.5 s. Optionally records the path you drive (a point every 1.5 m or on a turn), saves it, uploads it to `paths/`.
* **Auto mode**: loads `paths/<name>.csv` and follows it with a 4 m acceptance radius. Stops at the end, on GPS loss,
  when it leaves the path, when it stops making progress, or when the control page goes quiet.
* **Logging**: 5 Hz rows (time, lat, lon, fix quality, satellites, HDOP, speed, course, yaw, mode, battery,
  cross-track error, waypoint, event, and the IMU: raw accel x/y/z in m/s^2, raw gyro x/y/z in rad/s, pitch, roll)
  to flash, uploaded to `logs/` in batches while stopped. Offline-safe. Columns: see `logHeader()` in `github_log.cpp`;
  empty means "not available" (no fix, no IMU, no battery sensor, not in auto mode).
* **OTA updates**: bump the version, push, CI builds and publishes `ota/`. **At power-up, as soon as the rover is online,
  it checks for a newer version and installs it by itself** (then re-checks every 5 minutes while stopped). It verifies the
  MD5 first and stays disarmed afterwards. The control page also has **Check for update** and **Install update** buttons, and
  `auto_update=off` in `config/rover.cfg` switches the automatic install off (the button then installs on demand).
* **WiFi setup page**: no credentials in code. Hold the button 5 s at power-up, or use "Change WiFi".

## Repository layout

```
firmware/agri_rover/   the Arduino sketch: thin main + one module per job (below)
ota/                   version.txt + firmware.bin, written by CI, read by the rovers
paths/                 recorded or hand-made paths (CSV)
config/rover.cfg       runtime tuning, no rebuild needed
logs/                  uploaded rover logs (chunks, merged nightly into one file per rover per day)
test/                  host-side unit tests (run on a laptop, no ESP32 needed)
tools/                 build, release, sticker and CI helper scripts
docs/                  pin map, architecture, bring-up checklist, releasing
.github/workflows/     build + OTA publish, nightly log merge, weekly TLS-root check
```

### Firmware: one sketch, one module per job

Everything is in `firmware/agri_rover/`: **one `.ino`** (setup and loop only), **`config.h`**, **`ui.h`**, and **one
`.h`/`.cpp` pair per job**. Nothing else to hunt for.

| File | Job |
|---|---|
| `agri_rover.ino` | `setup()` and `loop()` only; wires the modules together |
| `config.h` | pins, limits, the version constant, runtime-config defaults. **No secrets.** |
| `motors` | 4 independent L298N channels (IN1/IN2 direction, PWM on EN), skid-steer mixing, slew limit, immediate stop |
| `imu` | MPU6050: bias calibration, tilt, heading (gyro, aligned by the GPS course) |
| `gps` | NEO-6M at 5 Hz: fix quality, satellites, HDOP (NMEA parser and u-blox setup included) |
| `wifi_manager` | saved network, setup page, captive portal, offline mode; the rover's identity and access code |
| `ota_github` | version check, download, MD5 verify and flash; the shared GitHub client; config sync |
| `github_log` | 5 Hz flash log, batched upload with retry and back-off |
| `path` | record, thin, save, load and follow (geometry and the path follower included) |
| `remote` + `ui.h` | web server and the control/setup pages (the HTML lives in `ui.h`) |
| `safety` | every failsafe as one function, the shared rover state, and the 100 Hz control task that applies them |
| `certs.h`, `partitions.csv` | GitHub's root certificates (data), and the two-slot OTA flash layout |

Inside each `.h`/`.cpp` the **top half is plain C++ with no Arduino dependency** (the GPS parser, geometry, path follower,
failsafe rules, version logic...), so it is unit-tested on a laptop; the part under `#ifdef ARDUINO` is what touches the hardware.

## First-time setup

1. **Wire it** per [docs/PINOUT.md](docs/PINOUT.md). It follows your running sketch, with **one wiring change**: the RR
   enable line moves from GPIO 21 (the IMU's I2C data pin) to GPIO 16. Remove the L298N EN jumpers and fit pull-downs on the 4 EN pins.
2. **Build and flash by USB once** (Arduino IDE 2 or arduino-cli, ESP32 core 3.1.x, ArduinoJson 7). Board
   "ESP32 Dev Module". The sketch folder carries its own `partitions.csv` (two OTA slots), which the IDE picks up
   automatically. After this first flash, updates are over the air.
   ```
   python tools/build_firmware.py        # needs arduino-cli; prints the size and the image path
   ```
3. **Power up.** With no saved WiFi the rover starts its own network `RoverSetup-xxxx`. The password is the rover's
   **access code**: print it for a label with `python tools/ap_password.py <MAC>` (the MAC is printed on the serial
   console at boot). Join it, open any web page, pick your WiFi, enter the password, and optionally a GitHub token.
   The rover **tests the connection and only then saves it**.
4. **GitHub token** (needed for uploads, and for OTA if the repo is private): create a *fine-grained* personal
   access token limited to this one repository with **Contents: Read and write**, and paste it on the setup page.
   It is stored only in the rover's flash. It is never printed, logged or committed.
5. Open the control page (`http://rover-xxxxxx.local`, or the IP shown on the setup page; on the rover's own
   network use `192.168.4.1`).

## Serial output (compatible with the original sketch)

The serial port (115200 baud) carries **JSON lines only**, one object per line, so a laptop logger that parses the old
`surveying_robot.ino` stream keeps working:

```
{"imu":{"accel_x":0.12,"accel_y":-0.03,"accel_z":9.81,"gyro_x":0.00,"gyro_y":0.01,"gyro_z":-0.02}}   every 500 ms (m/s^2, rad/s: same keys and units as before)
{"gps":{"fix":true,"quality":1,"sats":9,"hdop":1.1,"lat":31.5204000,"lon":74.3587000,"speed_mps":0.12,"course_deg":84.4}}   every 1 s
{"status":"INFO","message":"MPU6050 Initialized Successfully"}                                       diagnostics
{"status":"RUNNING","state":"MANUAL"}   {"status":"STOPPED","state":"DISARMED","reason":"LINK_LOST"}  mode changes
{"event":"OTA_START","detail":"0.2.1"}   {"event":"OTA_DONE","detail":"0.2.1"}                       updates
```

It never prints WiFi passwords, the GitHub token or the access code. `SERIAL_TELEMETRY` in `config.h` turns the periodic
imu/gps lines off. At every boot it also prints the IMU's I2C pins and says whether the IMU answered, which is how you confirm the
pins ([docs/PINOUT.md](docs/PINOUT.md), "Confirm the IMU pins").

## The brief, stage by stage

| Stage | Pass criterion | Implemented by | Verified |
|---|---|---|---|
| 0 | Builds; modules; PWM; motors off at boot; credentials out of repo; IMU stable with motors running | all modules; `motors.cpp` forces pins LOW first; credentials in NVS only | Builds clean (0 warnings). **Motors/IMU on hardware: not yet** (see checklist) |
| 1 | WiFi setup page; GitHub OTA; version bump reaches rover; bad download keeps old firmware | `wifi_manager`, `ota_github`, CI | Logic unit-tested; the "bad download" path is covered by MD5/size checks. **On a rover: not yet** |
| 2 | GPS lat/long logged to GitHub; known loop plots correctly | `gps`, `github_log` | NMEA parsing tested with real reference sentences. **Real GPS + upload: not yet** |
| 3 | Phone control with failsafe; stops on link loss | `remote`, `ui.h`, `control`, `safety` | Every failsafe rule unit-tested. **Phone on a real rover: not yet** |
| 4 | Record, save, upload a path | `path` | Thinning + file format tested. **Real recording: not yet** |
| 5 | Replay paths from the repo with error measurements | `follow`, `path`, `github_log` (logs `xte_m`, `wp`) | Closed-loop **simulation** with GPS noise and outliers. **Tuning on the real rover: not yet** |

[docs/BRINGUP.md](docs/BRINGUP.md) is the step-by-step checklist for turning "not yet" into "yes", with the
measurements to take at each stage.

## What has and has not been verified

**Verified (on a laptop and in CI):**
* The firmware compiles for `esp32:esp32:esp32` (core 3.1.3) with no warnings from this code.
* 83 host unit tests: NMEA parsing (real reference sentences, bad checksums, garbage, partial lines), u-blox frames,
  geometry, thinning, the path follower in closed-loop simulation (GPS noise up to 2 m, 3% multipath outliers,
  sharp corners, U-turns, closed loops, off-path, stuck, no-fix), every safety rule, OTA version/manifest logic,
  config clamping, motor mixing, and the access-code derivation (matches the sticker tool).
* 20 tooling tests: the OTA publisher, log consolidation, version bumping, and the compile-time pin checks (they reject the sketch's original GPIO 21 clash).
* GitHub's live TLS chains validate against the embedded roots (`tools/check_tls_roots.py`).

**Not verified: needs the real rover, and will need tuning:**
* The pin map against your real wiring (it comes from the sketch, not from a bench run), motor directions (`WHEEL_INVERT`), `MOTOR_MIN_DUTY`, and the L298N under stall (the EDD flags that a
  GA25-370 stall current of 1.8-4 A exceeds an L298N channel's ~2 A).
* IMU noise with the motors running; GPS performance with the new small patch antenna.
* `turn_full_dps` (how fast the rover really turns at full command). Auto-mode accuracy depends on it.
* The WiFi setup flow on a phone, the OTA flash itself, and uploads over a real connection.
* **The simulation is a simplified plant.** It proves the logic (no false faults, correct signs, termination); it does
  not prove the tuning. Expect to adjust `turn_full_dps` and the steering gains on the real rover.

**Known limits, stated plainly:**
* **No automatic rollback** if a new firmware boots but misbehaves: stock Arduino builds do not enable the
  bootloader's rollback support. A bad release is fixed by publishing a newer one (the rover still checks while
  stopped) or by USB. Test releases on one rover first.
* The GPS is ~2.5 m accurate (EDD s6.7): fine for geotagging and path replay, **not** row-level guidance.
* Battery sensing is **off** until a divider is fitted (the schematic has none): set `BATT_SENSE_ENABLED` in
  `config.h`. Until then there is no low-battery failsafe.
* The access code that protects the rover's own WiFi is derived from its MAC and a salt in `config.h`, so it keeps
  casual users out but is not strong secrecy: choose your own code on the setup page for anything sensitive.

## Deliberate design decisions (and why)

* **Logs are uploaded as chunks, merged nightly into one file per rover per day.** The brief asks for one file per
  rover per day. Appending to one growing file through the GitHub API means downloading and re-sending the whole file
  every time, which an ESP32 cannot do past a few hundred KB, and the EDD flags exactly this (s7.4). Chunks are small,
  idempotent and safe to retry; `tools/consolidate_logs.py` produces the single daily file.
* **Strict TLS, no `setInsecure()`.** The reference sketch disabled certificate checks (EDD s7.4). Here the rover
  validates GitHub against embedded roots and refuses to talk otherwise. A weekly CI job warns before a CA change can
  break that.
* **Failsafes live in a separate real-time task** that never touches flash or the network, so a slow upload or an OTA
  flash cannot delay a stop.
* **All runtime tuning is clamped.** A bad `config/rover.cfg` cannot, for example, raise the command timeout to a
  minute.

## How the EDD's review of the reference sketch was addressed (s7.4)

| Issue noted in the EDD | Here |
|---|---|
| ADC curve-fitting API does not exist on the WROOM-32 | Uses `analogReadMilliVolts()` on an ADC1 pin (GPIO34) |
| WiFi connect blocks forever | Non-blocking state machine, 15 s timeout, then setup mode, then offline mode |
| Token in the source | Flash only (NVS), entered on the setup page |
| `setInsecure()` and no checksum | Strict TLS; MD5 and size verified before the image can boot |
| OTA could run mid-drive | Only while disarmed; re-checked before reboot |
| Whole daily file re-sent on every push | Batched chunks, idempotent, resumable |
| Several draft versions in one file | One consolidated, tested module per job |
| Deep sleep unsuited to a driving rover | Removed |

## Open questions for the team

1. The **Stage 5 "error measurements below"** were cut off in the brief that was shared. The log already records
   cross-track error (`xte_m`) and waypoint index in auto mode; please send the exact measurements wanted
   (for example max/mean cross-track error, end-point error, time per lap) and where they should be reported.
2. **Confirm the GPIO 21 clash** (BR_EN vs the IMU's SDA) and the new GPS pins ([docs/PINOUT.md](docs/PINOUT.md)); the final map once the PCB revision settles.
3. **Battery chemistry and divider** (thresholds in `config/rover.cfg` assume 3S Li-ion).
4. **Motor driver**: the L298N is rated below the motor stall current; the final driver part is still open in the EDD.

## Contributing

* `./tools/test_host.sh` (or `tools\test_host.ps1` on Windows) runs the unit tests; no hardware needed.
* `python -m unittest tools.test_tools` runs the tooling tests.
* To release: `python tools/bump_version.py patch`, commit, push. See [docs/RELEASING.md](docs/RELEASING.md).
