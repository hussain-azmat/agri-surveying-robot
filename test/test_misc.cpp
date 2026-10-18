#include <math.h>
#include <string.h>

#include "wifi_manager.h"
#include "ota_github.h"
#include "mini_test.h"
#include "motors.h"
#include "safety.h"

// ------------------------------------------------------- access code ----

TEST(access_code_matches_the_python_sticker_tool) {
  // Golden vector from `python tools/ap_password.py A4:CF:12:34:56:78` with the default salt.
  // If this fails, tools/ap_password.py and wifi_manager.cpp have drifted apart: stickers would be wrong.
  char code[11];
  deriveAccessCode("agri-rover-v1", "A4CF12345678", code);
  CHECK(strcmp(code, "ue2u86hjju") == 0);
}

TEST(access_code_is_stable_distinct_and_unambiguous) {
  char a[11], b[11], c[11];
  deriveAccessCode("salt", "A4CF12345678", a);
  deriveAccessCode("salt", "A4CF12345678", b);
  deriveAccessCode("salt", "A4CF12345679", c);  // one MAC digit different
  CHECK(strcmp(a, b) == 0);
  CHECK(strcmp(a, c) != 0);
  CHECK(strlen(a) == 10);
  for (const char* p = a; *p; p++) CHECK(strchr("abcdefghjkmnpqrstuvwxyz23456789", *p) != nullptr);
  char d[11];
  deriveAccessCode("other", "A4CF12345678", d);  // changing the salt changes every code
  CHECK(strcmp(a, d) != 0);
}

// ---------------------------------------------------------- versions ----

TEST(semver_parsing) {
  SemVer v;
  CHECK(parseSemVer("1.2.3", &v) && v.major == 1 && v.minor == 2 && v.patch == 3);
  CHECK(parseSemVer("v10.0.7", &v) && v.major == 10 && v.patch == 7);
  CHECK(!parseSemVer("1.2", &v));
  CHECK(!parseSemVer("1.2.3.4", &v));
  CHECK(!parseSemVer("1.2.x", &v));
  CHECK(!parseSemVer("1.2.3-beta", &v));
  CHECK(!parseSemVer("", &v));
  CHECK(!parseSemVer(" 1.2.3", &v));
  CHECK(!parseSemVer("99999.0.0", &v));
  CHECK(!parseSemVer(nullptr, &v));
}

TEST(ota_updates_only_when_the_repo_version_is_newer) {
  CHECK(decideOta("0.1.0", "0.1.1") == OtaDecision::UPDATE);
  CHECK(decideOta("0.9.9", "0.10.0") == OtaDecision::UPDATE);   // numeric, not string, comparison
  CHECK(decideOta("1.9.0", "2.0.0") == OtaDecision::UPDATE);
  CHECK(decideOta("0.1.0", "0.1.0") == OtaDecision::UP_TO_DATE);
  CHECK(decideOta("0.2.0", "0.1.9") == OtaDecision::REMOTE_OLDER);  // "different" is NOT enough
  CHECK(decideOta("0.2.0", "garbage") == OtaDecision::INVALID);
  CHECK(decideOta("garbage", "0.2.0") == OtaDecision::INVALID);
}

TEST(ota_manifest_accepts_a_good_file_incl_crlf_comments_and_uppercase_md5) {
  const char* t =
      "# published by CI\r\n"
      "version=v1.4.2\r\n"
      "md5=0123456789ABCDEF0123456789abcdef\r\n"
      "size=1048576\r\n";
  OtaManifest m;
  const char* err = nullptr;
  CHECK(parseOtaManifest(t, &m, &err));
  CHECK(strcmp(m.version, "1.4.2") == 0);
  CHECK(strcmp(m.md5, "0123456789abcdef0123456789abcdef") == 0);
  CHECK(m.size == 1048576);
}

TEST(ota_manifest_refuses_anything_unverifiable) {
  OtaManifest m;
  const char* err = nullptr;
  CHECK(!parseOtaManifest("version=1.0.0\nsize=1048576\n", &m, &err));  // no md5: must not flash
  CHECK(err && strstr(err, "md5"));
  CHECK(!parseOtaManifest("version=1.0.0\nmd5=abc\nsize=1048576\n", &m, &err));
  CHECK(!parseOtaManifest("version=1.0.0\nmd5=zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz\nsize=1048576\n", &m, &err));
  CHECK(!parseOtaManifest("md5=0123456789abcdef0123456789abcdef\nsize=1048576\n", &m, &err));  // no version
  CHECK(!parseOtaManifest("version=1.0.0\nmd5=0123456789abcdef0123456789abcdef\n", &m, &err));  // no size
  CHECK(!parseOtaManifest("version=1.0.0\nmd5=0123456789abcdef0123456789abcdef\nsize=12\n", &m, &err));  // tiny
  CHECK(!parseOtaManifest("version=1.0.0\nmd5=0123456789abcdef0123456789abcdef\nsize=99999999999\n", &m, &err));
  CHECK(!parseOtaManifest("version=latest\nmd5=0123456789abcdef0123456789abcdef\nsize=1048576\n", &m, &err));
  CHECK(!parseOtaManifest("<html>404: Not Found</html>", &m, &err));  // a GitHub error page is not a manifest
  CHECK(!parseOtaManifest("", &m, &err));
  CHECK(!parseOtaManifest(nullptr, &m, &err));
}

// ----------------------------------------------------------- config ----

TEST(rt_config_defaults_and_overrides) {
  RuntimeConfig c;
  CHECK_NEAR(c.autoSpeed, DEF_AUTO_SPEED, 1e-6);
  RtConfigReport r = parseRtConfig("auto_speed=0.5\nmax_speed = 0.9  # fast\n\n# comment\nlogging=off\n", &c);
  CHECK(r.applied == 3 && r.malformed == 0 && r.ignored == 0);
  CHECK_NEAR(c.autoSpeed, 0.5, 1e-6);
  CHECK_NEAR(c.maxSpeed, 0.9, 1e-6);
  CHECK(!c.loggingEnabled);
}

TEST(rt_config_auto_update_switch) {
  RuntimeConfig c;
  CHECK(c.autoUpdate);  // on by default: the rover installs a newer firmware by itself
  parseRtConfig("auto_update=off\n", &c);
  CHECK(!c.autoUpdate);
  parseRtConfig("auto_update=on\n", &c);
  CHECK(c.autoUpdate);
  RtConfigReport r = parseRtConfig("auto_update=maybe\n", &c);
  CHECK(r.malformed == 1 && c.autoUpdate);  // junk leaves the value unchanged
}

TEST(rt_config_clamps_so_a_bad_file_cannot_make_the_rover_unsafe) {
  RuntimeConfig c;
  RtConfigReport r = parseRtConfig(
      "cmd_timeout_ms=60000\n"      // a 60 s command timeout would defeat the failsafe
      "accept_radius_m=0.1\n"       // sub-GPS-accuracy radius would make waypoints unreachable
      "max_speed=50\n"
      "link_timeout_ms=1\n",
      &c);
  CHECK(r.clamped == 4);
  CHECK(c.cmdTimeoutMs == 1000);
  CHECK_NEAR(c.acceptRadiusM, 3.0, 1e-6);
  CHECK_NEAR(c.maxSpeed, 1.0, 1e-6);
  CHECK(c.linkTimeoutMs == 1000);
}

TEST(rt_config_keeps_cross_field_invariants) {
  RuntimeConfig c;
  parseRtConfig("accept_radius_m=12\noffpath_m=5\nbatt_low_v=10\nbatt_crit_v=11\n", &c);
  CHECK(c.offPathM >= c.acceptRadiusM + 2.0f);
  CHECK(c.battCritV < c.battLowV);
}

TEST(rt_config_reports_junk_without_applying_it) {
  RuntimeConfig c;
  const float before = c.autoSpeed;
  RtConfigReport r = parseRtConfig("auto_speed=fast\nunknown_key=3\nnonsense line\nlogging=maybe\n=5\n", &c);
  CHECK(c.autoSpeed == before);
  CHECK(r.applied == 0);
  CHECK(r.malformed >= 3);
  CHECK(r.ignored == 1);
  RuntimeConfig d;
  parseRtConfig("auto_speed=nan\nmax_speed=inf\n", &d);  // non-finite must not slip through strtod
  CHECK(d.autoSpeed == DEF_AUTO_SPEED && d.maxSpeed == DEF_MAX_SPEED);
}

TEST(rt_config_survives_a_huge_or_binary_input) {
  RuntimeConfig c;
  char junk[5000];
  for (int i = 0; i < 4999; i++) junk[i] = (char)(i % 251 + 1);
  junk[4999] = 0;
  parseRtConfig(junk, &c);
  CHECK(c.autoSpeed >= 0.1f && c.autoSpeed <= 0.8f);
  parseRtConfig(nullptr, &c);  // must not crash
}

// ------------------------------------------------------------ mixer ----

TEST(mixer_straight_pivot_and_turn) {
  WheelSpeeds s = mixSkidSteer(0.5f, 0.0f);
  CHECK(s.fl == 0.5f && s.fr == 0.5f && s.rl == 0.5f && s.rr == 0.5f);
  WheelSpeeds p = mixSkidSteer(0.0f, 0.6f);  // pivot clockwise: left forward, right backward
  CHECK(p.fl > 0 && p.rl > 0 && p.fr < 0 && p.rr < 0);
  CHECK_NEAR(p.fl, -p.fr, 1e-6);
  WheelSpeeds t = mixSkidSteer(0.5f, 0.2f);  // gentle right turn: left side faster
  CHECK(t.fl > t.fr);
}

TEST(mixer_normalises_instead_of_clipping_one_side) {
  WheelSpeeds s = mixSkidSteer(1.0f, 1.0f);  // would be left=2, right=0
  CHECK(fabsf(s.fl) <= 1.0f && fabsf(s.fr) <= 1.0f);
  CHECK_NEAR(s.fl, 1.0, 1e-6);
  CHECK_NEAR(s.fr, 0.0, 1e-6);
  WheelSpeeds q = mixSkidSteer(0.9f, 0.6f);  // left 1.5, right 0.3 -> scaled together, ratio kept
  CHECK_NEAR(q.fr / q.fl, 0.3 / 1.5, 1e-5);
}

TEST(mixer_clamps_wild_inputs_and_applies_the_limit) {
  WheelSpeeds s = mixSkidSteer(50.0f, -50.0f, 0.5f);
  CHECK(fabsf(s.fl) <= 0.5f + 1e-6f && fabsf(s.fr) <= 0.5f + 1e-6f);
  WheelSpeeds z = mixSkidSteer(NAN, NAN);  // garbage in -> stopped out
  CHECK(z.fl == 0.0f && z.fr == 0.0f && z.rl == 0.0f && z.rr == 0.0f);
  WheelSpeeds i = mixSkidSteer(INFINITY, 0.0f);
  CHECK(i.fl == 0.0f && i.fr == 0.0f);
}

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
