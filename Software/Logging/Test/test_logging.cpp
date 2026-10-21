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
}  // namespace

TEST(log_row_has_exactly_as_many_fields_as_the_header_in_every_situation) {
  const int headerCommas = countCommas(logHeader());
  CHECK(strstr(logHeader(), ",accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z,pitch_deg,roll_deg\n") != nullptr);
  char row[320];
  GpsData g;
  ImuData im;
  // nothing available at all: no fix, no IMU, no battery, no clock
  CHECK(formatLogRow(row, sizeof(row), "", 5, "OFF", false, 0, false, 0, 0, g, im, "") > 0);
  CHECK(countCommas(row) == headerCommas);
  // everything available, AUTO
  g.fix = true; g.quality = 1; g.sats = 9; g.hdop = 1.1f; g.lat = 31.5204123; g.lon = 74.3587456; g.speedMps = 0.85f;
  g.courseValid = true; g.courseDeg = 84.4f;
  im.ok = true; im.yawValid = true; im.yawDeg = 83.6f; im.pitchDeg = 1.5f; im.rollDeg = -2.25f;
  im.accelX = 0.12f; im.accelY = -0.03f; im.accelZ = 9.81f; im.gyroX = 0.001f; im.gyroY = 0.01f; im.gyroZ = -0.0234f;
  CHECK(formatLogRow(row, sizeof(row), "2026-10-20T06:12:03.200Z", 184220, "AUTO", true, 12.34f, true, 0.31f, 12, g, im, "ARM:AUTO") > 0);
  CHECK(countCommas(row) == headerCommas);
}

TEST(log_row_content_and_order) {
  GpsData g;
  g.fix = true; g.quality = 1; g.sats = 9; g.hdop = 1.1f; g.lat = 31.5204123; g.lon = 74.3587456; g.speedMps = 0.85f;
  g.courseValid = true; g.courseDeg = 84.4f;
  ImuData im;
  im.ok = true; im.yawValid = true; im.yawDeg = 83.6f; im.pitchDeg = 1.5f; im.rollDeg = -2.25f;
  im.accelX = 0.12f; im.accelY = -0.03f; im.accelZ = 9.81f; im.gyroX = 0.001f; im.gyroY = 0.01f; im.gyroZ = -0.0234f;
  char row[320];
  const size_t n = formatLogRow(row, sizeof(row), "2026-10-20T06:12:03.200Z", 184220, "AUTO", false, 0, true, 0.31f, 12, g, im, "");
  CHECK(n == strlen(row));
  CHECK(strcmp(row, "2026-10-20T06:12:03.200Z,184220,31.5204123,74.3587456,1,9,1.1,0.85,84,83.6,AUTO,,0.31,12,,"
                    "0.12,-0.03,9.81,0.001,0.010,-0.023,1.5,-2.2\n") == 0 ||
        strcmp(row, "2026-10-20T06:12:03.200Z,184220,31.5204123,74.3587456,1,9,1.1,0.85,84,83.6,AUTO,,0.31,12,,"
                    "0.12,-0.03,9.81,0.001,0.010,-0.023,1.5,-2.3\n") == 0);
  CHECK(row[n - 1] == '\n');
}

TEST(log_row_leaves_unavailable_fields_empty_not_zero) {
  GpsData g;  // no fix
  ImuData im; // not answering
  char row[320];
  formatLogRow(row, sizeof(row), "", 5, "MAN", false, 0, false, 9, 9, g, im, "");
  // no position, no battery, no xte/wp (not AUTO), no IMU values
  CHECK(strstr(row, ",MAN,,,,,,,,,,,,\n") != nullptr);
}

TEST(log_row_never_prints_nan_and_reports_overflow) {
  GpsData g;
  ImuData im;
  im.ok = true;
  im.accelX = NAN; im.gyroZ = INFINITY; im.pitchDeg = NAN;
  char row[320];
  CHECK(formatLogRow(row, sizeof(row), "t", 1, "MAN", false, 0, false, 0, 0, g, im, "") > 0);
  CHECK(!strstr(row, "nan") && !strstr(row, "inf") && !strstr(row, "NAN"));
  char tiny[20];
  CHECK(formatLogRow(tiny, sizeof(tiny), "t", 1, "MAN", false, 0, false, 0, 0, g, im, "") == 0);  // does not fit: nothing half-written
  CHECK(tiny[0] == '\0');
}
