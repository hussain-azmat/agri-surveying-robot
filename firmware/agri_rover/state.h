#pragma once
// Shared rover state. The control task (core 1, 100 Hz) writes sensors and applies
// failsafes; the main loop (web, network, flash) reads it and requests changes. Every
// access goes through StateLock, and every mode change goes through the functions below,
// so there is exactly one place where "armed" can become true.
#include <Arduino.h>

#include "config.h"
#include "follow.h"
#include "rt_config.h"
#include "safety.h"

struct GpsData {
  bool healthy = false;      // NMEA is arriving at all
  bool fix = false;          // a position valid right now (fresh, quality > 0)
  uint8_t quality = 0;
  uint8_t sats = 0;
  float hdop = 99.9f;
  double lat = 0, lon = 0;
  float altM = 0;
  float speedMps = 0;
  float courseDeg = 0;
  bool courseValid = false;
  uint32_t fixAgeMs = 0xFFFFFFFF;
  bool timeValid = false;
  uint16_t year = 0;
  uint8_t month = 0, day = 0, hour = 0, minute = 0, second = 0;
  uint32_t fixCount = 0;     // increments on every new GGA fix
  float rateHz = 0;          // measured GGA rate; should read ~5.0 once configured
  uint32_t baud = 0;
  uint32_t badChecksum = 0;
};

struct ImuData {
  bool ok = false;           // sensor answering
  bool calibrated = false;   // gyro bias known
  bool calibrating = false;
  bool yawValid = false;     // heading aligned to the world (needs a GPS course once)
  float yawDeg = 0;          // compass heading, clockwise from north
  float yawRateDps = 0;      // clockwise positive
  float pitchDeg = 0, rollDeg = 0, tiltDeg = 0;
  float gyroNoiseDps = 0;    // short-term gyro noise: watch it rise when the motors run (EDD stage 0)
  uint32_t errors = 0;
};

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

  bool recording = false;
  uint16_t recPoints = 0;
  RuntimeConfig cfg;

  // Wiring check ("bench") request: spin ONE wheel briefly, only while DISARMED.
  int8_t benchWheel = -1;
  float benchValue = 0;
  uint32_t benchUntilMs = 0;
};

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

SafetyInput stateSafetyInput(const RoverState& s, uint32_t nowMs);
SafetyParams stateSafetyParams(const RuntimeConfig& cfg);
