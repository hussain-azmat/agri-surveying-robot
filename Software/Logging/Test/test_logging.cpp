// Host unit tests: Logging (serial JSON lines and CSV log rows).
#include <math.h>
#include <string>
#include <string.h>
#include "github_log.h"
#include "mini_test.h"

// ------------------------------------------------------- serial JSON ----

namespace {
// Minimal structural check: one object, braces balanced, every string closed, no raw control characters.
bool looksLikeJsonObject(const char* s) {
  const size_t n = strlen(s);
  if (n < 2 || s[0] != '{' || s[n - 1] != '}') return false;
  int depth = 0;
  bool inStr = false;
  for (size_t i = 0; i < n; i++) {
    const unsigned char c = (unsigned char)s[i];
    if (c < 0x20) return false;
    if (inStr) {
      if (c == '\\') i++;
      else if (c == '"') inStr = false;
    } else if (c == '"') inStr = true;
    else if (c == '{') depth++;
    else if (c == '}' && --depth < 0) return false;
  }
  return depth == 0 && !inStr;
}
}  // namespace

TEST(serial_imu_line_matches_the_original_sketch_format_exactly) {
  // Same keys, same order, 2 decimals, accel in m/s^2 and gyro in rad/s, as surveying_robot.ino printed.
  ImuData d;
  d.accelX = 0.12f; d.accelY = -0.03f; d.accelZ = 9.81f;
  d.gyroX = 0.0f; d.gyroY = 0.01f; d.gyroZ = -0.02f;
  char b[200];
  const size_t n = formatImuJson(b, sizeof(b), d);
  CHECK(strcmp(b, "{\"imu\":{\"accel_x\":0.12,\"accel_y\":-0.03,\"accel_z\":9.81,"
                  "\"gyro_x\":0.00,\"gyro_y\":0.01,\"gyro_z\":-0.02}}") == 0);
  CHECK(n == strlen(b));
}

TEST(serial_json_never_prints_nan_or_infinity) {
  ImuData d;
  d.accelX = NAN; d.gyroZ = INFINITY;
  char b[200];
  formatImuJson(b, sizeof(b), d);
  CHECK(!strstr(b, "nan") && !strstr(b, "inf") && !strstr(b, "NAN"));
  CHECK(looksLikeJsonObject(b));
}

TEST(serial_gps_line_with_and_without_a_fix) {
  GpsData g;
  char b[300];
  formatGpsJson(b, sizeof(b), g);
  CHECK(strstr(b, "\"fix\":false") && !strstr(b, "\"lat\""));  // no position is invented without a fix
  CHECK(looksLikeJsonObject(b));
  g.fix = true; g.quality = 1; g.sats = 9; g.hdop = 1.1f; g.lat = 31.5204; g.lon = 74.3587; g.speedMps = 0.12f; g.courseDeg = 84.4f;
  formatGpsJson(b, sizeof(b), g);
  CHECK(strstr(b, "\"fix\":true") && strstr(b, "\"lat\":31.5204000") && strstr(b, "\"lon\":74.3587000"));
  CHECK(looksLikeJsonObject(b));
}

TEST(serial_status_message_and_event_lines) {
  char b[300];
  formatStatusJson(b, sizeof(b), "RUNNING", "MANUAL", nullptr);
  CHECK(strcmp(b, "{\"status\":\"RUNNING\",\"state\":\"MANUAL\"}") == 0);
  formatStatusJson(b, sizeof(b), "STOPPED", "DISARMED", "LINK_LOST");
  CHECK(strcmp(b, "{\"status\":\"STOPPED\",\"state\":\"DISARMED\",\"reason\":\"LINK_LOST\"}") == 0);
  formatStatusJson(b, sizeof(b), "READY", nullptr, nullptr);
  CHECK(strcmp(b, "{\"status\":\"READY\"}") == 0);
  formatMessageJson(b, sizeof(b), "INFO", "MPU6050 Initialized Successfully");  // the original sketch's exact text
  CHECK(strcmp(b, "{\"status\":\"INFO\",\"message\":\"MPU6050 Initialized Successfully\"}") == 0);
  formatEventJson(b, sizeof(b), "OTA_START", nullptr);
  CHECK(strcmp(b, "{\"event\":\"OTA_START\"}") == 0);  // the original sketch's OTA event, byte for byte
  formatEventJson(b, sizeof(b), "OTA_START", "0.2.1");
  CHECK(strcmp(b, "{\"event\":\"OTA_START\",\"detail\":\"0.2.1\"}") == 0);
}

TEST(serial_json_escapes_hostile_text_so_the_line_stays_one_valid_object) {
  char b[400];
  formatMessageJson(b, sizeof(b), "INFO", "he said \"stop\"\nand\\then\ttabs \x01 done");
  CHECK(looksLikeJsonObject(b));
  CHECK(!strchr(b, '\n'));  // an embedded newline must not split the line
  char tiny[16];
  CHECK(jsonEscape("abc\"def\"ghi", tiny, sizeof(tiny)) < sizeof(tiny));  // never overruns
  CHECK(strlen(tiny) < sizeof(tiny));
  char one[2];
  CHECK(jsonEscape("hello", one, sizeof(one)) < sizeof(one));
  CHECK(jsonEscape(nullptr, one, sizeof(one)) == 0);
}

TEST(serial_long_text_is_cut_before_formatting_not_in_the_middle_of_the_line) {
  std::string huge(1000, 'x');
  huge += "\"";
  char b[400];
  formatMessageJson(b, sizeof(b), "ERROR", huge.c_str());
  CHECK(looksLikeJsonObject(b));
}

// --------------------------------------------------------------- log rows ----

namespace {
int countCommas(const char* s) {
  int n = 0;
  for (; *s; s++) n += *s == ',';
  return n;
}

LogSample fullSample() {
  LogSample s;
  s.sec = 1792995123;  // 2026-10-26T06:12:03Z
  s.usec = 200000;
  s.uptimeMs = 184220;
  s.gps.fix = true; s.gps.quality = 1; s.gps.sats = 9; s.gps.hdop = 1.1f; s.gps.lat = 31.5204123; s.gps.lon = 74.3587456;
  s.gps.altM = 214.3f; s.gps.speedMps = 0.85f; s.gps.courseValid = true; s.gps.courseDeg = 84.4f;
  s.gps.fixCount = 5231; s.gps.fixAgeMs = 40; s.gps.timeValid = true; s.gps.hour = 6; s.gps.minute = 12; s.gps.second = 3;
  s.imu.ok = true; s.imu.yawValid = true; s.imu.yawDeg = 83.6f; s.imu.pitchDeg = 1.5f; s.imu.rollDeg = -2.25f;
  s.win.n = 20; s.win.ax = 0.12f; s.win.ay = -0.03f; s.win.az = 9.81f; s.win.gx = 0.001f; s.win.gy = 0.01f; s.win.gz = -0.0234f;
  s.win.gyroPeak = 0.052f;
  s.driftValid = true; s.driftDeg = 0.4f; s.straightS = 12.3f;
  s.mode = LogMode::AUTO; s.throttle = 0.6f; s.turn = 0.0f; s.xteM = 0.31f; s.wp = 12;
  return s;
}
}  // namespace

TEST(log_row_has_exactly_as_many_fields_as_the_header_in_every_situation) {
  const int headerCommas = countCommas(logHeader());
  CHECK(headerCommas == 31);  // 32 columns
  char row[420];
  LogSample empty;  // nothing available at all: no fix, no IMU, no battery, no clock
  CHECK(formatLogRow(row, sizeof(row), empty) > 0);
  CHECK(countCommas(row) == headerCommas);
  CHECK(formatLogRow(row, sizeof(row), fullSample()) > 0);
  CHECK(countCommas(row) == headerCommas);
  LogSample ev = fullSample();
  strcpy(ev.event, "DISARM:LINK_LOST");
  CHECK(formatLogRow(row, sizeof(row), ev) > 0);
  CHECK(countCommas(row) == headerCommas);
}

TEST(log_row_content_and_order) {
  char row[420];
  const size_t n = formatLogRow(row, sizeof(row), fullSample());
  CHECK(n == strlen(row));
  CHECK(strcmp(row, "2026-10-26T06:12:03.200Z,184220,5231,40,06:12:03,31.5204123,74.3587456,214.3,1,9,1.1,0.85,84,83.6,1.5,-2.2,"
                    "0.12,-0.03,9.81,0.001,0.010,-0.023,0.052,0.40,12.3,AUTO,0.60,0.00,,0.31,12,\n") == 0 ||
        strcmp(row, "2026-10-26T06:12:03.200Z,184220,5231,40,06:12:03,31.5204123,74.3587456,214.3,1,9,1.1,0.85,84,83.6,1.5,-2.3,"
                    "0.12,-0.03,9.81,0.001,0.010,-0.023,0.052,0.40,12.3,AUTO,0.60,0.00,,0.31,12,\n") == 0);
  CHECK(row[n - 1] == '\n');
}

TEST(log_row_leaves_unavailable_fields_empty_not_zero) {
  char row[420];
  LogSample s;  // no fix, IMU not answering, no GPS time, clock not set, not armed
  s.uptimeMs = 5;
  formatLogRow(row, sizeof(row), s);
  // utc,uptime,fix_n,fix_age,gps_time,lat,lon,alt  then fix,sats,hdop,speed then 4 empty angles, 7 empty IMU, drift, straight
  CHECK(strncmp(row, ",5,0,,,,,,0,0,99.9,0.00,", 24) == 0);
  CHECK(strstr(row, ",,,,,,,,,,,,,,OFF,0.00,0.00,,,,\n") != nullptr);  // empty IMU block, drift, straight, then OFF
}

TEST(log_row_drift_only_inside_a_straight_run) {
  char row[420];
  LogSample s = fullSample();
  s.driftValid = false;
  formatLogRow(row, sizeof(row), s);
  CHECK(strstr(row, "0.052,,,AUTO,") != nullptr);  // gyro_peak, then empty drift_deg and straight_s
  s.driftValid = true; s.driftDeg = -1.25f; s.straightS = 3.0f;
  formatLogRow(row, sizeof(row), s);
  CHECK(strstr(row, "0.052,-1.25,3.0,AUTO,") != nullptr);
}

TEST(log_row_course_and_heading_are_empty_until_valid) {
  char row[420];
  LogSample s = fullSample();
  s.gps.courseValid = false;
  s.imu.yawValid = false;
  formatLogRow(row, sizeof(row), s);
  CHECK(strstr(row, ",0.85,,,1.5,") != nullptr);  // speed, empty course, empty yaw, pitch
}

TEST(log_row_never_prints_nan_and_reports_overflow) {
  LogSample s = fullSample();
  s.win.ax = NAN; s.win.gz = INFINITY; s.imu.pitchDeg = NAN; s.gps.hdop = NAN; s.throttle = INFINITY;
  char row[420];
  CHECK(formatLogRow(row, sizeof(row), s) > 0);
  CHECK(!strstr(row, "nan") && !strstr(row, "inf") && !strstr(row, "NAN") && !strstr(row, "INF"));
  char tiny[20];
  CHECK(formatLogRow(tiny, sizeof(tiny), s) == 0);  // does not fit: nothing half-written
  CHECK(tiny[0] == '\0');
}

TEST(log_row_event_text_cannot_break_the_csv) {
  LogSample s = fullSample();
  strcpy(s.event, "a,b\nc");
  char row[420];
  formatLogRow(row, sizeof(row), s);
  CHECK(countCommas(row) == 31);
  CHECK(strchr(row, '\n') == row + strlen(row) - 1);  // the only newline is the last character
}

TEST(utc_formatting_is_right_across_dates_and_unset_clock) {
  char o[40], d[9];
  CHECK(formatUtc(o, sizeof(o), 1792995123, 200000, d) > 0);
  CHECK(strcmp(o, "2026-10-26T06:12:03.200Z") == 0 && strcmp(d, "20261026") == 0);
  formatUtc(o, sizeof(o), 1709164800, 0, d);  // 2024-02-29 00:00:00: a leap day
  CHECK(strcmp(o, "2024-02-29T00:00:00.000Z") == 0 && strcmp(d, "20240229") == 0);
  formatUtc(o, sizeof(o), 1767225599, 999999, d);  // 2025-12-31 23:59:59.999
  CHECK(strcmp(o, "2025-12-31T23:59:59.999Z") == 0);
  CHECK(formatUtc(o, sizeof(o), 5, 0, d) == 0 && o[0] == '\0' && strcmp(d, "00000000") == 0);  // clock not set
}

TEST(preamble_carries_what_produced_the_file) {
  RuntimeConfig c;
  c.logEvery = 5; c.logAlways = true;
  char p[320];
  CHECK(formatLogPreamble(p, sizeof(p), "0.4.0", "rover-A1B2C3", c) > 0);
  CHECK(p[0] == '#' && strstr(p, "schema=2") && strstr(p, "fw=0.4.0") && strstr(p, "rover=rover-A1B2C3"));
  CHECK(strstr(p, "log_every=5") && strstr(p, "log_always=1"));
  CHECK(strchr(p, '\n') == p + strlen(p) - 1);
  formatLogPreamble(p, sizeof(p), "0.4.0", "bad\nid,x", c);  // an id with a newline must not split the line
  CHECK(strchr(p, '\n') == p + strlen(p) - 1);
}

TEST(header_block_is_the_comment_lines_plus_the_header_line) {
  const char* f = "# schema=2 fw=0.4.0\n# second\nutc,uptime_ms\n2026-10-26T06:12:03.200Z,1\n";
  CHECK(logHeaderBlockLen(f, strlen(f)) == strlen("# schema=2 fw=0.4.0\n# second\nutc,uptime_ms\n"));
  const char* plain = "utc,uptime_ms\nrow\n";
  CHECK(logHeaderBlockLen(plain, strlen(plain)) == strlen("utc,uptime_ms\n"));
  CHECK(logHeaderBlockLen("# only a comment\n", 17) == 0);          // no header line yet
  CHECK(logHeaderBlockLen("utc,uptime", 10) == 0);                  // torn header
}

TEST(sample_ring_keeps_order_and_counts_what_it_had_to_drop) {
  SampleRing<4> r;
  LogSample s, o;
  for (int i = 1; i <= 3; i++) { s.uptimeMs = i; r.push(s); }
  CHECK(r.size() == 3);
  CHECK(r.pop(&o) && o.uptimeMs == 1);
  CHECK(r.pop(&o) && o.uptimeMs == 2);
  CHECK(r.takeDropped() == 0);
  for (int i = 4; i <= 9; i++) { s.uptimeMs = i; r.push(s); }  // 1 left + 6 pushed into 4 slots: 3 oldest dropped
  CHECK(r.size() == 4);
  CHECK(r.takeDropped() == 3);
  CHECK(r.takeDropped() == 0);  // reading the count resets it
  CHECK(r.pop(&o) && o.uptimeMs == 6);  // the oldest survivors, still in order
  CHECK(r.pop(&o) && o.uptimeMs == 7 && r.pop(&o) && o.uptimeMs == 8 && r.pop(&o) && o.uptimeMs == 9);
  CHECK(!r.pop(&o));
}
