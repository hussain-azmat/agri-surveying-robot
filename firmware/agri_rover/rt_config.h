#pragma once
// PURE: no Arduino dependencies. Unit-tested on a laptop (test/test_misc.cpp).
//
// Runtime configuration: config/rover.cfg in the repo, fetched by the rover and cached
// in flash, so tuning needs NO rebuild. Plain "key=value" lines ('#' comments).
//
// Every value is CLAMPED to a safe range. A typo or a malicious edit of the file can
// make the rover less capable, but not unsafe (e.g. the command timeout can never be
// set above 1 s, the acceptance radius never below 3 m).
#include <stddef.h>
#include <stdint.h>

#include "config.h"

struct RuntimeConfig {
  float maxSpeed = DEF_MAX_SPEED;           // 0.1 .. 1.0   manual throttle scale
  float autoSpeed = DEF_AUTO_SPEED;         // 0.1 .. 0.8
  float acceptRadiusM = DEF_ACCEPT_RADIUS_M;  // 3 .. 15
  float offPathM = DEF_OFFPATH_M;           // acceptRadius+2 .. 50
  uint32_t cmdTimeoutMs = DEF_CMD_TIMEOUT_MS;    // 200 .. 1000
  uint32_t linkTimeoutMs = DEF_LINK_TIMEOUT_MS;  // 1000 .. 10000
  float hdopMax = DEF_HDOP_MAX;             // 1 .. 20
  float battLowV = DEF_BATT_LOW_V;          // 6 .. 30
  float battCritV = DEF_BATT_CRIT_V;        // 5 .. battLow
  float pathSpacingM = DEF_PATH_SPACING_M;  // 1 .. 5
  float pathTurnDeg = DEF_PATH_TURN_DEG;    // 5 .. 45
  uint32_t uploadIdleS = DEF_UPLOAD_IDLE_S;      // 5 .. 600
  uint32_t syncIntervalS = DEF_SYNC_INTERVAL_S;  // 30 .. 3600
  uint32_t otaIntervalS = DEF_OTA_INTERVAL_S;    // 60 .. 86400
  float turnFullDps = 90.0f;                // 20 .. 360  yaw rate at turn = 1 (calibrate!)
  bool loggingEnabled = true;
};

struct RtConfigReport {
  int applied = 0;     // known keys accepted (possibly after clamping)
  int clamped = 0;     // of those, values that had to be clamped
  int ignored = 0;     // unknown keys
  int malformed = 0;   // lines that were not key=value or had unparsable values
};

// Starts from `cfg` as given (normally the defaults) and applies the file on top.
// Never fails outright: bad lines are skipped and counted in the report.
RtConfigReport parseRtConfig(const char* text, RuntimeConfig* cfg);
// Re-establishes cross-field invariants (e.g. critical < low, offPath > accept).
void normalizeRtConfig(RuntimeConfig* cfg);
