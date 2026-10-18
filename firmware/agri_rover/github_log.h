#pragma once
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
#include <Arduino.h>

#include "state.h"

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
