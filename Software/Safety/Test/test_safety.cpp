// Host unit tests: Safety (every failsafe rule).
#include <math.h>
#include <string.h>
#include "mini_test.h"
#include "motors.h"
#include "safety.h"

// ----------------------------------------------------------- safety ----

namespace {
SafetyInput armedManual(uint32_t now = 10000) {
  SafetyInput in;
  in.mode = Mode::MANUAL;
  in.nowMs = now;
  in.lastCmdMs = now - 50;
  in.lastHbMs = now - 50;
  in.imuOk = true;
  in.yawValid = true;
  in.gpsFix = true;
  in.gpsAgeMs = 100;
  in.hdop = 1.2f;
  in.follow = FollowState::IDLE;
  return in;
}
SafetyInput armedAuto(uint32_t now = 10000) {
  SafetyInput in = armedManual(now);
  in.mode = Mode::AUTO;
  in.follow = FollowState::RUNNING;
  return in;
}
}  // namespace

TEST(safety_disarmed_never_allows_motion) {
  SafetyParams p;
  SafetyMemory m;
  SafetyInput in = armedManual();
  in.mode = Mode::DISARMED;
  SafetyOutput o = safetyEvaluate(in, p, &m);
  CHECK(!o.allowMotion && !o.disarm);
}

TEST(safety_manual_runs_while_commands_arrive) {
  SafetyParams p;
  SafetyMemory m;
  SafetyOutput o = safetyEvaluate(armedManual(), p, &m);
  CHECK(o.allowMotion && !o.disarm);
}

TEST(safety_manual_stops_after_half_a_second_without_commands_and_resumes) {
  SafetyParams p;
  SafetyMemory m;
  SafetyInput in = armedManual(10000);
  in.lastCmdMs = 9400;  // 600 ms ago
  SafetyOutput o = safetyEvaluate(in, p, &m);
  CHECK(!o.allowMotion);
  CHECK(!o.disarm);  // stays armed
  CHECK(strcmp(o.reason, "CMD_TIMEOUT") == 0);
  in.lastCmdMs = 9990;  // a fresh command arrives
  CHECK(safetyEvaluate(in, p, &m).allowMotion);
}

TEST(safety_timeouts_survive_millis_wraparound) {
  SafetyParams p;
  SafetyMemory m;
  SafetyInput in = armedManual();
  in.nowMs = 100;                // millis() wrapped ...
  in.lastCmdMs = 0xFFFFFFF0u;    // ... 116 ms after the command: still fresh
  in.lastHbMs = 0xFFFFFFF0u;
  CHECK(safetyEvaluate(in, p, &m).allowMotion);
  in.nowMs = 2000;               // 2.1 s later: stale
  CHECK(!safetyEvaluate(in, p, &m).allowMotion);
}

TEST(safety_auto_link_loss_disarms) {
  SafetyParams p;
  SafetyMemory m;
  SafetyInput in = armedAuto();
  CHECK(safetyEvaluate(in, p, &m).allowMotion);
  in.lastHbMs = in.nowMs - 3500;
  SafetyOutput o = safetyEvaluate(in, p, &m);
  CHECK(!o.allowMotion && o.disarm && strcmp(o.reason, "LINK_LOST") == 0);
}

TEST(safety_auto_gps_loss_holds_then_disarms) {
  SafetyParams p;
  SafetyMemory m;
  SafetyInput in = armedAuto(10000);
  in.gpsFix = false;
  SafetyOutput o = safetyEvaluate(in, p, &m);
  CHECK(!o.allowMotion && !o.disarm);  // inside the grace window: hold still
  in.nowMs = 10800;
  in.lastCmdMs = in.lastHbMs = 10750;
  o = safetyEvaluate(in, p, &m);
  CHECK(!o.allowMotion && !o.disarm);
  in.nowMs = 11700;  // 1.7 s without a fix
  in.lastCmdMs = in.lastHbMs = 11650;
  o = safetyEvaluate(in, p, &m);
  CHECK(o.disarm && strcmp(o.reason, "NO_FIX") == 0);
}

TEST(safety_auto_gps_blip_does_not_disarm) {
  SafetyParams p;
  SafetyMemory m;
  SafetyInput in = armedAuto(10000);
  in.gpsFix = false;
  safetyEvaluate(in, p, &m);
  in.nowMs = 10400;
  in.gpsFix = true;  // back after 400 ms
  in.lastHbMs = 10350;
  SafetyOutput o = safetyEvaluate(in, p, &m);
  CHECK(o.allowMotion && !o.disarm);
  in.nowMs = 12000;  // the old bad-since timestamp must not linger
  in.lastHbMs = 11950;
  CHECK(safetyEvaluate(in, p, &m).allowMotion);
}

TEST(safety_auto_poor_hdop_counts_as_no_fix) {
  SafetyParams p;
  SafetyMemory m;
  SafetyInput in = armedAuto(10000);
  in.hdop = 12.0f;
  safetyEvaluate(in, p, &m);
  in.nowMs = 12000;
  in.lastHbMs = 11950;
  SafetyOutput o = safetyEvaluate(in, p, &m);
  CHECK(o.disarm && strcmp(o.reason, "NO_FIX") == 0);
}

TEST(safety_auto_stops_on_every_follower_outcome) {
  SafetyParams p;
  struct { FollowState s; const char* reason; } cases[] = {
      {FollowState::FINISHED, "PATH_DONE"},        {FollowState::FAULT_OFFPATH, "OFF_PATH"},
      {FollowState::FAULT_STUCK, "STUCK"},         {FollowState::FAULT_NOFIX, "NO_FIX"},
      {FollowState::FAULT_NOHEADING, "NO_HEADING"}, {FollowState::FAULT_BADPATH, "BAD_PATH"},
      {FollowState::IDLE, "NO_PATH"}};
  for (auto& c : cases) {
    SafetyMemory m;
    SafetyInput in = armedAuto();
    in.follow = c.s;
    SafetyOutput o = safetyEvaluate(in, p, &m);
    CHECK(!o.allowMotion && o.disarm);
    CHECK(strcmp(o.reason, c.reason) == 0);
  }
}

TEST(safety_auto_requires_a_valid_heading) {
  SafetyParams p;
  SafetyMemory m;
  SafetyInput in = armedAuto();
  in.yawValid = false;
  SafetyOutput o = safetyEvaluate(in, p, &m);
  CHECK(o.disarm && strcmp(o.reason, "NO_HEADING") == 0);
  in.yawValid = true;
  in.imuOk = false;
  CHECK(safetyEvaluate(in, p, &m).disarm);
}

TEST(safety_rollover_disarms_after_a_short_debounce_not_on_a_bump) {
  SafetyParams p;
  SafetyMemory m;
  SafetyInput in = armedManual(10000);
  in.tiltDeg = 70;
  SafetyOutput o = safetyEvaluate(in, p, &m);
  CHECK(!o.disarm);  // a single sample (a bump) is not a rollover
  in.nowMs = 10100;
  in.lastCmdMs = 10090;
  CHECK(!safetyEvaluate(in, p, &m).disarm);
  in.nowMs = 10400;
  in.lastCmdMs = 10390;
  o = safetyEvaluate(in, p, &m);
  CHECK(o.disarm && strcmp(o.reason, "TILT") == 0);
  // a bump that recovers resets the timer
  SafetyMemory m2;
  in = armedManual(20000);
  in.tiltDeg = 70;
  safetyEvaluate(in, p, &m2);
  in.nowMs = 20100;
  in.lastCmdMs = 20090;
  in.tiltDeg = 5;
  safetyEvaluate(in, p, &m2);
  in.nowMs = 20500;
  in.lastCmdMs = 20490;
  in.tiltDeg = 70;
  CHECK(!safetyEvaluate(in, p, &m2).disarm);
}

TEST(safety_battery_only_acts_when_the_sensor_is_fitted) {
  SafetyParams p;
  SafetyMemory m;
  SafetyInput in = armedManual(10000);
  in.battKnown = false;
  in.battV = 0.0f;  // a floating, unconnected ADC pin
  safetyEvaluate(in, p, &m);
  in.nowMs = 14000;
  in.lastCmdMs = 13990;
  CHECK(!safetyEvaluate(in, p, &m).disarm);

  SafetyMemory m2;
  in = armedManual(10000);
  in.battKnown = true;
  in.battV = 9.0f;
  CHECK(!safetyEvaluate(in, p, &m2).disarm);  // debounced: needs > 2 s
  in.nowMs = 12500;
  in.lastCmdMs = 12490;
  SafetyOutput o = safetyEvaluate(in, p, &m2);
  CHECK(o.disarm && strcmp(o.reason, "BATT_CRIT") == 0);
}

TEST(safety_can_arm_and_start_auto_preconditions) {
  SafetyParams p;
  const char* why = "";
  SafetyInput in = armedAuto();
  in.mode = Mode::DISARMED;
  CHECK(safetyCanArm(in, p, &why));
  CHECK(safetyCanStartAuto(in, p, &why));

  SafetyInput tilted = in;
  tilted.tiltDeg = 40;
  CHECK(!safetyCanArm(tilted, p, &why));

  SafetyInput noGps = in;
  noGps.gpsFix = false;
  CHECK(!safetyCanStartAuto(noGps, p, &why) && strstr(why, "GPS"));
  CHECK(safetyCanArm(noGps, p, &why));  // manual driving does not need GPS

  SafetyInput noYaw = in;
  noYaw.yawValid = false;
  CHECK(!safetyCanStartAuto(noYaw, p, &why) && strstr(why, "heading"));

  SafetyInput lowBatt = in;
  lowBatt.battKnown = true;
  lowBatt.battV = 9.7f;
  CHECK(!safetyCanArm(lowBatt, p, &why));
}
