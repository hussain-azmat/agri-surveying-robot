#pragma once
// Safety: every failsafe as ONE pure function (unit-tested), the shared rover state, and the 100 Hz
// control task that applies them. (Top half: plain C++. Under #ifdef ARDUINO: state locking and the task.)
#include <stdint.h>
#include "config.h"
#include "gps.h"
#include "imu.h"
#include "path.h"

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

struct RoverState {
  Mode mode = Mode::DISARMED;
  char stopReason[24] = "BOOT";  // why the rover is disarmed, for the UI
  uint32_t modeSinceMs = 0;

  float cmdThrottle = 0, cmdTurn = 0;
  uint32_t lastCmdMs = 0;  // last drive command
  uint32_t lastHbMs = 0;   // last sign of life from the controlling page

  GpsData gps;
  ImuData imu;
  bool battKnown = false;
  float battV = 0;

  FollowStatus follow;
  float wheel[W_COUNT] = {0, 0, 0, 0};  // speeds actually applied
  // Veer from a straight line while moving with no turn commanded (see StraightMonitor in imu.h); + = to the right.
  bool driftValid = false;
  float driftDeg = 0;
  float straightS = 0;

  bool recording = false;
  uint16_t recPoints = 0;
  RuntimeConfig cfg;

  // Wiring check ("bench") request: spin ONE wheel briefly, only while DISARMED.
  int8_t benchWheel = -1;
  float benchValue = 0;
  uint32_t benchUntilMs = 0;
};

SafetyInput stateSafetyInput(const RoverState& s, uint32_t nowMs);
SafetyParams stateSafetyParams(const RuntimeConfig& cfg);

#ifdef ARDUINO
#include <Arduino.h>

// Shared rover state. The control task (core 1, 100 Hz) writes sensors and applies
// failsafes; the main loop (web, network, flash) reads it and requests changes. Every
// access goes through StateLock, and every mode change goes through the functions below,
// so there is exactly one place where "armed" can become true.








extern RoverState g_state;

void stateBegin();

class StateLock {
 public:
  StateLock();
  ~StateLock();
  StateLock(const StateLock&) = delete;
  StateLock& operator=(const StateLock&) = delete;
};

RoverState stateSnapshot();

// ---- Mode transitions (each takes the lock itself) ------------------------------------------
// Refuses (returns false, *why = static string) if the rover is not in a safe state.
bool stateArm(const char** why);
void stateDisarm(const char* reason);  // always succeeds; motors stop on the next control tick
// Switches DISARMED/MANUAL -> AUTO. The path module must already have started the follower.
bool stateStartAuto(const char** why);
// A drive command from the page. Also counts as a heartbeat.
void stateDrive(float throttle, float turn);
void stateHeartbeat();

SafetyParams stateSafetyParams(const RuntimeConfig& cfg);

// The real-time control task. It owns the sensors (IMU, GPS, battery), applies EVERY failsafe via
// safetyEvaluate(), runs the path follower and drives the motors, at 100 Hz on core 1 with its
// own watchdog.
//
// It never touches flash, WiFi or the network, so a slow GitHub upload, an OTA flash or a stalled
// web request can never delay a stop: if the web side hangs, commands stop arriving and this task
// stops the rover by itself within the command timeout.

void controlStart();
#endif
