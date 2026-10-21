Logging  -  serial JSON lines and the 5 Hz log uploaded to GitHub
=================================================================

WHAT IT DOES
  1. Serial output (115200 baud) is JSON lines only, in the style of the original surveying_robot.ino:
       {"imu":{"accel_x":..,"gyro_z":..}}  every 500 ms        {"gps":{"fix":true,"lat":..,"lon":..}}  every 1 s
       {"status":"INFO","message":".."}    diagnostics         {"event":"OTA_START","detail":"0.3.0"}  events
     It never prints the WiFi password, the GitHub token or the access code.
  2. The LOG: a CSV row at 5 Hz while the rover is armed, written to flash (about once a second, never per row), and
     uploaded to GitHub in batches while it is stopped and online. Columns:
       utc, uptime_ms, lat, lon, fix, sats, hdop, speed_mps, course_deg, yaw_deg, mode, batt_v, xte_m, wp, event,
       accel_x, accel_y, accel_z, gyro_x, gyro_y, gyro_z, pitch_deg, roll_deg
     Empty means "not available" (no fix, IMU not answering, no battery sensor, not in AUTO). accel in m/s^2, gyro in rad/s.
     Uploads: logs/<rover-id>/<YYYY-MM-DD>/<seq>_<chunk>.csv, 16 KB per chunk, resumable and safe to repeat; a nightly job
     (consolidate-logs workflow) merges each day into logs/<rover-id>/<YYYY-MM-DD>.csv. Uploading needs a GitHub token
     on the rover; without one the rows simply stay in flash (and logging stops, visibly, if the flash fills up).

FOLDER
  Src/github_log.h, github_log.cpp   the module (serial formatters and the CSV row formatter are plain C++)
  Test/test_logging.cpp              laptop unit tests: every JSON line, hostile text, every log row situation
  Bin/                               no stand-alone image: logging runs inside the full firmware (see Rover)

PASS CRITERIA
  - Laptop: the unit tests pass (bash tools/test_host.sh).
  - Rover: arm, drive a known loop, disarm, wait about 20 s -> a chunk appears under logs/<rover>/<today>/ with the
    columns above; plotting lat,lon shows the loop you walked.
