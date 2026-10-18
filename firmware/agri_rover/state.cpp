#include "state.h"

#include <freertos/semphr.h>
#include <string.h>

RoverState g_state;
static SemaphoreHandle_t g_mux = nullptr;

void stateBegin() {
  if (!g_mux) g_mux = xSemaphoreCreateMutex();
  g_state.cfg = RuntimeConfig();
  g_state.modeSinceMs = millis();
}

StateLock::StateLock() {
  if (g_mux) xSemaphoreTake(g_mux, portMAX_DELAY);
}
StateLock::~StateLock() {
  if (g_mux) xSemaphoreGive(g_mux);
}

RoverState stateSnapshot() {
  StateLock l;
  return g_state;
}

SafetyParams stateSafetyParams(const RuntimeConfig& c) {
  SafetyParams p;
  p.cmdTimeoutMs = c.cmdTimeoutMs;
  p.linkTimeoutMs = c.linkTimeoutMs;
  p.hdopMax = c.hdopMax;
  p.tiltLimitDeg = IMU_TILT_LIMIT_DEG;
  p.battCritV = c.battCritV;
  return p;
}

SafetyInput stateSafetyInput(const RoverState& s, uint32_t nowMs) {
  SafetyInput in;
  in.mode = s.mode;
  in.nowMs = nowMs;
  in.lastCmdMs = s.lastCmdMs;
  in.lastHbMs = s.lastHbMs;
  in.gpsFix = s.gps.fix;
  in.gpsAgeMs = s.gps.fixAgeMs;
  in.hdop = s.gps.hdop;
  in.imuOk = s.imu.ok;
  in.yawValid = s.imu.yawValid;
  in.tiltDeg = s.imu.tiltDeg;
  in.battKnown = s.battKnown;
  in.battV = s.battV;
  in.follow = s.follow.state;
  return in;
}

static void setMode(Mode m, const char* reason) {
  g_state.mode = m;
  g_state.modeSinceMs = millis();
  strncpy(g_state.stopReason, reason ? reason : "", sizeof(g_state.stopReason) - 1);
  g_state.stopReason[sizeof(g_state.stopReason) - 1] = '\0';
  g_state.cmdThrottle = g_state.cmdTurn = 0;
  g_state.benchWheel = -1;
}

bool stateArm(const char** why) {
  StateLock l;
  const char* dummy;
  if (!why) why = &dummy;
  if (g_state.mode != Mode::DISARMED) {
    *why = "already armed";
    return false;
  }
  const SafetyInput in = stateSafetyInput(g_state, millis());
  if (!safetyCanArm(in, stateSafetyParams(g_state.cfg), why)) return false;
  setMode(Mode::MANUAL, "");
  g_state.lastCmdMs = 0;  // no motion until the first fresh command arrives
  g_state.lastHbMs = millis();
  *why = "";
  return true;
}

void stateDisarm(const char* reason) {
  StateLock l;
  if (g_state.mode == Mode::DISARMED) {
    if (reason && *reason) strncpy(g_state.stopReason, reason, sizeof(g_state.stopReason) - 1);
    return;
  }
  setMode(Mode::DISARMED, reason);
}

bool stateStartAuto(const char** why) {
  StateLock l;
  const char* dummy;
  if (!why) why = &dummy;
  const SafetyInput in = stateSafetyInput(g_state, millis());
  if (!safetyCanStartAuto(in, stateSafetyParams(g_state.cfg), why)) return false;
  setMode(Mode::AUTO, "");
  g_state.lastHbMs = millis();
  *why = "";
  return true;
}

void stateDrive(float throttle, float turn) {
  StateLock l;
  if (!isfinite(throttle) || !isfinite(turn)) {  // garbage from the network means "stop"
    throttle = turn = 0;
  }
  g_state.cmdThrottle = constrain(throttle, -1.0f, 1.0f);
  g_state.cmdTurn = constrain(turn, -1.0f, 1.0f);
  g_state.lastCmdMs = millis();
  g_state.lastHbMs = g_state.lastCmdMs;
}

void stateHeartbeat() {
  StateLock l;
  g_state.lastHbMs = millis();
}
