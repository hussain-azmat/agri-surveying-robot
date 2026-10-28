// Host unit tests: OTA (versions, manifest, runtime config file).
#include <math.h>
#include <string>
#include <string.h>
#include "mini_test.h"
#include "ota_github.h"

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

TEST(rt_config_log_rate_and_log_always) {
  RuntimeConfig c;
  CHECK(c.logEvery == 1 && !c.logAlways);  // defaults: a row per fix, only while armed
  parseRtConfig("log_every=5\nlog_always=on\n", &c);
  CHECK(c.logEvery == 5 && c.logAlways);
  RtConfigReport r = parseRtConfig("log_every=0\n", &c);
  CHECK(c.logEvery == 1 && r.clamped == 1);  // clamped, never zero (that would mean "never log")
  r = parseRtConfig("log_every=9999\n", &c);
  CHECK(c.logEvery == 50 && r.clamped == 1);
  r = parseRtConfig("log_always=maybe\n", &c);
  CHECK(r.malformed == 1 && c.logAlways);  // junk leaves the setting alone
  parseRtConfig("log_always=off\nlogging=off\n", &c);
  CHECK(!c.logAlways && !c.loggingEnabled);  // the two switches are independent
}
