#pragma once
#include <stddef.h>
#include <stdint.h>

#include "gps.h"
#include "imu.h"

// ---- Serial JSON lines (pure formatters: unit-tested on a laptop) ---------------------------------
// The serial port carries one JSON object per line, in the style of the original surveying_robot.ino, so a laptop
// logger that parses that stream keeps working. Each formatter writes a NUL-terminated line WITHOUT a newline into
// `out` and returns its length (never more than cap-1). Text fields are limited BEFORE the line is built, so with a
// buffer of 400 bytes the line is always complete, valid JSON.
size_t jsonEscape(const char* in, char* out, size_t cap);  // quotes, backslashes and control characters
// {"imu":{"accel_x":0.12,"accel_y":-0.03,"accel_z":9.81,"gyro_x":0.00,"gyro_y":0.01,"gyro_z":-0.02}}  (m/s^2, rad/s)
size_t formatImuJson(char* out, size_t cap, const ImuData& d);
// {"gps":{"fix":true,"quality":1,"sats":9,"hdop":1.1,"lat":31.5204000,"lon":74.3587000,"speed_mps":0.12,"course_deg":84.4}}
size_t formatGpsJson(char* out, size_t cap, const GpsData& g);
// {"status":"RUNNING","state":"MANUAL","reason":""}   (state and reason are optional: pass nullptr)
size_t formatStatusJson(char* out, size_t cap, const char* status, const char* state, const char* reason);
// {"status":"INFO","message":"IMU I2C pins: SDA=GPIO21 SCL=GPIO22"}
size_t formatMessageJson(char* out, size_t cap, const char* status, const char* message);
// {"event":"OTA_START","detail":"0.2.1"}   (detail optional)
size_t formatEventJson(char* out, size_t cap, const char* event, const char* detail);

#ifdef ARDUINO
#include <Arduino.h>
#include "safety.h"

// ---- Serial output API (any module may call these; each call is ONE write, so lines never interleave) ----
void serialLog(const char* status, const char* fmt, ...) __attribute__((format(printf, 2, 3)));  // status: INFO/WARN/ERROR
void serialStatus(const char* status, const char* state, const char* reason);
void serialEvent(const char* event, const char* detail);
// Main loop, every iteration: prints the periodic {"imu":...} and {"gps":...} lines (SERIAL_TELEMETRY in config.h).
void serialTelemetry(const RoverState& s);

// Rover log: 5 Hz rows written to flash while armed, uploaded to GitHub in BATCHES when stopped.
//
// Rows (CSV): utc, uptime_ms, lat, lon, fix, sats, hdop, speed_mps, course_deg, yaw_deg, mode,
//             batt_v, xte_m, wp, event
//
//  * Rows go to /log/cur.csv in flash, buffered so flash is written about once a second, never per row.
//  * When the rover has been stopped for a while (and is online) the data is queued and uploaded as
//    chunks of ~16 KB, one GitHub commit per chunk: logs/<rover>/<YYYY-MM-DD>/<seq>_<chunk>.csv.
//    A nightly workflow (consolidate-logs.yml) merges each day's chunks into ONE file per rover per
//    day: logs/<rover>/<YYYY-MM-DD>.csv. (Appending to a growing file over the API would mean
//    re-downloading and re-sending the whole file each time, which cannot work on an ESP32.)
//  * Uploads retry with exponential back-off, survive reboots (progress is kept in flash) and are
//    idempotent: a chunk whose confirmation was lost is simply recognised as already uploaded.
//  * If offline, nothing is lost: rows stay in flash and are sent later. If flash is nearly full,
//    logging stops (and the UI says so) rather than corrupting or silently dropping data.
//  * Never uploads while the rover can move.


struct LogStatus {
  uint32_t pendingBytes;   // in flash, not yet uploaded
  uint32_t queuedFiles;
  uint32_t rowsLogged;     // since boot
  bool full;               // logging stopped because flash is nearly full
  char lastError[96];
  uint32_t lastUploadAgoS; // 0xFFFFFFFF if none since boot
};

void logBegin();
// Main loop, every iteration: writes a row at 5 Hz while armed. Cheap when nothing is due.
void logTick(const RoverState& s);
// Records a state change (e.g. "DISARM:LINK_LOST") as a row with the event column set.
void logEvent(const RoverState& s, const char* event);
void logFlush();
void logService(bool online, bool idle);
LogStatus logStatus();
#endif
