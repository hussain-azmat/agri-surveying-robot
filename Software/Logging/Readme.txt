Logging  -  serial JSON lines and the log uploaded to GitHub
============================================================

WHAT IT DOES
  1. Serial output (115200 baud) is JSON lines only, in the style of the original surveying_robot.ino:
       {"imu":{"accel_x":..,"gyro_z":..}}  every 500 ms        {"gps":{"fix":true,"lat":..,"lon":..}}  every 1 s
       {"status":"INFO","message":".."}    diagnostics         {"event":"OTA_START","detail":"0.4.0"}  events
     It never prints the WiFi password, the GitHub token or the access code.
  2. The LOG: one CSV row for every GPS fix (5 per second by default), written to flash and uploaded to GitHub in batches
     while the rover is stopped and online.

HOW A ROW IS MADE
  The control task (100 Hz) reads the IMU every 10 ms and averages those readings between two rows, so one row carries
  the MEAN accel / gyro of about 20 readings plus the PEAK gyro, not one lucky sample. When a new GPS fix arrives it builds a
  row from the fix, the averaged IMU, the commands applied to the motors and the state, and queues it. The main loop
  formats it and writes it to flash. If the main loop is busy (an upload, an update) the queue holds 64 rows (about 13 s); if it
  ever overflows, the oldest are dropped and a row with event GAP:n says how many, so a gap can never pass unnoticed.
  If the GPS goes silent, a row is still written every second (fix = 0).

WHEN IT LOGS
  While armed (default). With log_always=on in config/rover.cfg it also logs while disarmed, from power-up: use it to record
  a GPS accuracy test or an IMU drift test with the rover standing still. log_every=N writes a row for every Nth fix
  (N = 5 gives 1 row per second). logging=off stops it.

FILE LAYOUT   (schema 2)
  # schema=2 fw=0.4.0 rover=rover-A1B2C3 log_every=1 log_always=0 max_speed=0.60 auto_speed=0.45 accept_radius_m=4.0 turn_full_dps=90
  utc,uptime_ms,fix_n,fix_age_ms,gps_time,lat,lon,alt_m,fix,sats,hdop,speed_mps,course_deg,yaw_deg,pitch_deg,roll_deg,
  accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z,gyro_peak,drift_deg,straight_s,mode,throttle,turn,batt_v,xte_m,wp,event
  The first line says what produced the file (firmware, rover, settings); read the CSV with pandas.read_csv(path, comment="#").
  Empty means "not available", never 0.

  utc            time of the row from the rover's clock (set from WiFi time or the GPS)
  uptime_ms      milliseconds since boot
  fix_n          the GPS fix counter: it goes up by one per fix, so a repeated or skipped fix is visible
  fix_age_ms     age of the position when the row was made
  gps_time       time of day the GPS reported (HH:MM:SS UTC)
  lat, lon, alt_m  position (7 decimals) and altitude in metres; empty without a fix
  fix, sats, hdop  fix quality (0 = none), satellites, horizontal dilution of precision
  speed_mps, course_deg  GPS speed and course over ground (course empty while it is not valid)
  yaw_deg        fused heading, clockwise from north (empty until aligned); pitch_deg, roll_deg  tilt
  accel_x/y/z    MEAN raw acceleration since the previous row, m/s^2
  gyro_x/y/z     MEAN raw angular rate since the previous row, rad/s
  gyro_peak      the largest angular-rate magnitude in that window, rad/s (shows shocks and vibration)
  drift_deg      how far the rover has veered from a straight line while it was NOT being told to turn (see below)
  straight_s     how long that straight run has lasted
  mode           OFF / MAN / REC (manual while recording a path) / AUTO
  throttle, turn the commands applied to the motors, -1 .. 1 (what the operator or the follower asked for)
  batt_v         battery volts (empty until the divider is fitted)
  xte_m, wp      cross-track error and waypoint index (AUTO only)
  event          ARM:MANUAL, ARM:AUTO, DISARM:<reason>, GAP:<n rows lost>; empty on ordinary rows

STRAIGHT-LINE DRIFT
  A "straight run" is: moving (|throttle| >= 0.1) with no turn commanded (|turn| <= 0.02) and the IMU answering. During a run
  the gyro yaw rate is integrated, so drift_deg is the exact angle the rover has turned since the run began, whatever the cause
  (unequal motors, a slipping wheel, a bump, a slope). Positive = veered to the right. The first half second of a run is skipped
  (the rover is still finishing a turn or speeding up) and the value appears after one second. A turn pressed on the remote
  ends the run, so deliberate turns never count. It does not depend on the GPS, and a GPS heading correction does not disturb it.
  Accuracy is limited by the gyro's residual bias, a few hundredths of a degree per second. The control page shows it as one
  quiet line, "Straight-line drift", among the telemetry.

UPLOAD
  Files go to logs/<rover>/<YYYY-MM-DD>/<seq>_<chunk>.csv, 16 KB per chunk, resumable and safe to repeat; every chunk repeats the
  metadata and header lines so it stands alone. A nightly job (consolidate-logs workflow) merges each day into
  logs/<rover>/<YYYY-MM-DD>.csv, keeping the metadata line(s) and one header. Uploading needs a GitHub token on the rover;
  without one the rows stay in flash.

HOW MUCH FITS
  A row is about 170 bytes, so 5 rows/s is about 50 KB a minute: the rover's flash holds roughly 17 minutes if it cannot upload.
  The control page shows the estimated minutes left (System card). When the flash is nearly full logging stops, visibly, rather
  than overwriting anything. log_every=5 makes it about 85 minutes.

FOLDER
  Src/github_log.h, github_log.cpp   serial formatters, the row / header / metadata formatters (plain C++), and the flash writer
  Src/log_sample.h                   one row's data, and the queue between the control task and the writer (plain C++)
  Test/test_logging.cpp              laptop unit tests: every JSON line, every row situation, the queue, the time format
  Bin/                               no stand-alone image: logging runs inside the full firmware (see Rover)

PASS CRITERIA
  - Laptop: the unit tests pass (bash tools/test_host.sh).
  - Rover: arm, drive a known loop, disarm, wait about 20 s -> a chunk appears under logs/<rover>/<today>/ with the metadata
    line, the 32 columns above and one row per GPS fix (fix_n goes up by 1 each row at 5 rows/s); plotting lat,lon shows the loop.
  - Hold the rover still with log_always=on: gyro and accel means are steady, drift_deg stays empty (not moving).
  - Drive straight on a flat floor for 10 s with no button but forward: drift_deg and straight_s fill in; press a turn
    button and they empty.
