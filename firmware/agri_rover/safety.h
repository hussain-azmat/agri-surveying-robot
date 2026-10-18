#pragma once
// PURE: no Arduino dependencies. Unit-tested on a laptop (test/test_misc.cpp).
//
// Failsafe policy as ONE pure function, so every rule can be tested without hardware.
// The control task calls safetyEvaluate() every 10 ms with the latest sensor/command
// snapshot and obeys the result: if !allowMotion the motors are stopped immediately;
// if disarm is set the rover also drops to DISARMED and must be armed again by a person.
//
// Policy (EDD s7.3 Task 4 + brief s6):
//   MANUAL  : commands older than cmdTimeoutMs        -> stop (stay armed; resumes with new commands)
//   AUTO    : heartbeat older than linkTimeoutMs      -> stop + disarm   (link loss)
//             GPS fix lost / stale / HDOP too high    -> stop + disarm   (after a short grace)
//             heading unavailable                     -> stop + disarm
//             follower fault (off path, stuck, ...)   -> stop + disarm
//             end of path                             -> stop + disarm   (normal completion)
//   ANY     : tilt beyond limit (rolled over)         -> stop + disarm
//             battery below critical for > 2 s        -> stop + disarm   (only if the sensor is fitted)
//   DISARMED: never allows motion.
#include <stdint.h>

#include "follow.h"

enum class Mode : uint8_t { DISARMED = 0, MANUAL = 1, AUTO = 2 };

struct SafetyParams {
  uint32_t cmdTimeoutMs = 500;
  uint32_t linkTimeoutMs = 3000;
  uint32_t gpsGraceMs = 1500;
  float hdopMax = 5.0f;
  float tiltLimitDeg = 50.0f;
  float battCritV = 9.6f;
  uint32_t battDebounceMs = 2000;
  uint32_t tiltDebounceMs = 300;
};

struct SafetyInput {
  Mode mode = Mode::DISARMED;
  uint32_t nowMs = 0;
  uint32_t lastCmdMs = 0;   // when the last drive command arrived
  uint32_t lastHbMs = 0;    // when the last heartbeat (any request from the page) arrived
  bool gpsFix = false;
  uint32_t gpsAgeMs = 0xFFFFFFFF;
  float hdop = 99.9f;
  bool imuOk = false;
  bool yawValid = false;
  float tiltDeg = 0;
  bool battKnown = false;
  float battV = 0;
  FollowState follow = FollowState::IDLE;
};

// Mutable debounce memory owned by the caller (one per control loop).
struct SafetyMemory {
  uint32_t gpsBadSinceMs = 0;
  bool gpsBad = false;
  uint32_t battLowSinceMs = 0;
  bool battLow = false;
  uint32_t tiltSinceMs = 0;
  bool tilted = false;
};

struct SafetyOutput {
  bool allowMotion = false;
  bool disarm = false;
  const char* reason = "";  // static string; "" when everything is fine
};

SafetyOutput safetyEvaluate(const SafetyInput& in, const SafetyParams& p, SafetyMemory* mem);

// Pre-conditions for a person to arm / start auto. `why` receives a static string on refusal.
bool safetyCanArm(const SafetyInput& in, const SafetyParams& p, const char** why);
bool safetyCanStartAuto(const SafetyInput& in, const SafetyParams& p, const char** why);
