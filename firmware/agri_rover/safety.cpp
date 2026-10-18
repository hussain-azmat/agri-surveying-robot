// PURE: no Arduino dependencies.
#include "safety.h"

namespace {
// Wrap-safe "a happened more than `ms` ago".
inline bool olderThan(uint32_t now, uint32_t then, uint32_t ms) { return (uint32_t)(now - then) > ms; }
}  // namespace

SafetyOutput safetyEvaluate(const SafetyInput& in, const SafetyParams& p, SafetyMemory* mem) {
  SafetyOutput out;

  // --- Debounced conditions are tracked in every mode so they are warm when arming. ---
  const bool tiltNow = in.imuOk && in.tiltDeg > p.tiltLimitDeg;
  if (tiltNow) {
    if (!mem->tilted) {
      mem->tilted = true;
      mem->tiltSinceMs = in.nowMs;
    }
  } else {
    mem->tilted = false;
  }

  const bool battNow = in.battKnown && in.battV < p.battCritV;
  if (battNow) {
    if (!mem->battLow) {
      mem->battLow = true;
      mem->battLowSinceMs = in.nowMs;
    }
  } else {
    mem->battLow = false;
  }

  const bool gpsGoodNow = in.gpsFix && in.gpsAgeMs <= 1500 && in.hdop <= p.hdopMax;
  if (!gpsGoodNow) {
    if (!mem->gpsBad) {
      mem->gpsBad = true;
      mem->gpsBadSinceMs = in.nowMs;
    }
  } else {
    mem->gpsBad = false;
  }

  if (in.mode == Mode::DISARMED) {
    out.reason = "";
    return out;  // allowMotion=false, disarm=false
  }

  // --- Rules that apply to every armed mode (these latch by disarming). ---
  if (mem->tilted && olderThan(in.nowMs, mem->tiltSinceMs, p.tiltDebounceMs)) {
    out.disarm = true;
    out.reason = "TILT";
    return out;
  }
  if (mem->battLow && olderThan(in.nowMs, mem->battLowSinceMs, p.battDebounceMs)) {
    out.disarm = true;
    out.reason = "BATT_CRIT";
    return out;
  }

  if (in.mode == Mode::MANUAL) {
    if (olderThan(in.nowMs, in.lastCmdMs, p.cmdTimeoutMs)) {
      out.allowMotion = false;  // stop, but stay armed so driving resumes with the next command
      out.reason = "CMD_TIMEOUT";
      return out;
    }
    out.allowMotion = true;
    return out;
  }

  // --- AUTO ---
  if (olderThan(in.nowMs, in.lastHbMs, p.linkTimeoutMs)) {
    out.disarm = true;
    out.reason = "LINK_LOST";
    return out;
  }
  if (in.follow == FollowState::FINISHED) {
    out.disarm = true;
    out.reason = "PATH_DONE";
    return out;
  }
  if (in.follow != FollowState::RUNNING) {
    out.disarm = true;
    out.reason = in.follow == FollowState::IDLE ? "NO_PATH" : followStateName(in.follow);
    return out;
  }
  if (!in.imuOk || !in.yawValid) {
    out.disarm = true;
    out.reason = "NO_HEADING";
    return out;
  }
  if (mem->gpsBad) {
    if (olderThan(in.nowMs, mem->gpsBadSinceMs, p.gpsGraceMs)) {
      out.disarm = true;
      out.reason = "NO_FIX";
      return out;
    }
    out.allowMotion = false;  // inside the grace window: hold still, do not give up yet
    out.reason = "GPS_HOLD";
    return out;
  }
  out.allowMotion = true;
  return out;
}

bool safetyCanArm(const SafetyInput& in, const SafetyParams& p, const char** why) {
  const char* dummy;
  if (!why) why = &dummy;
  if (in.imuOk && in.tiltDeg > 25.0f) {
    *why = "rover is tilted";
    return false;
  }
  if (in.battKnown && in.battV < p.battCritV + 0.3f) {
    *why = "battery too low";
    return false;
  }
  *why = "";
  return true;
}

bool safetyCanStartAuto(const SafetyInput& in, const SafetyParams& p, const char** why) {
  const char* dummy;
  if (!why) why = &dummy;
  if (!safetyCanArm(in, p, why)) return false;
  if (!(in.gpsFix && in.gpsAgeMs <= 1500)) {
    *why = "no GPS fix";
    return false;
  }
  if (in.hdop > p.hdopMax) {
    *why = "GPS accuracy too low (HDOP)";
    return false;
  }
  if (!in.imuOk) {
    *why = "IMU not responding";
    return false;
  }
  if (!in.yawValid) {
    *why = "heading not aligned: drive forward ~5 m in manual first";
    return false;
  }
  *why = "";
  return true;
}
