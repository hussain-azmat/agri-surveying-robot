#include <stdio.h>
#include <string.h>

#include <string>

#include "mini_test.h"
#include "gps.h"

namespace {

// Builds "$<body>*HH\r\n" with the correct checksum.
std::string sentence(const std::string& body) {
  unsigned x = 0;
  for (char c : body) x ^= (unsigned char)c;
  char tail[8];
  snprintf(tail, sizeof(tail), "*%02X\r\n", x);
  return "$" + body + tail;
}

uint8_t feedAll(NmeaParser& p, const std::string& s) {
  uint8_t ev = 0;
  for (char c : s) ev |= p.feed(c);
  return ev;
}

// Real-world reference sentences (checksums as published in the NMEA examples).
const char* kGga = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n";
const char* kRmc = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\r\n";

}  // namespace

TEST(nmea_parses_reference_gga) {
  NmeaParser p;
  CHECK(feedAll(p, kGga) == NMEA_GGA);
  const NmeaData& d = p.data();
  CHECK(d.posValid);
  CHECK(d.quality == 1);
  CHECK(d.sats == 8);
  CHECK_NEAR(d.hdop, 0.9, 1e-4);
  CHECK_NEAR(d.altM, 545.4, 1e-3);
  CHECK_NEAR(d.lat, 48.0 + 7.038 / 60.0, 1e-9);
  CHECK_NEAR(d.lon, 11.0 + 31.0 / 60.0, 1e-9);
}

TEST(nmea_parses_reference_rmc) {
  NmeaParser p;
  CHECK(feedAll(p, kRmc) == NMEA_RMC);
  const NmeaData& d = p.data();
  CHECK(d.rmcActive);
  CHECK_NEAR(d.speedMps, 22.4 * 0.514444, 1e-3);
  CHECK_NEAR(d.courseDeg, 84.4, 1e-3);
  CHECK(d.courseValid);
  CHECK(d.timeValid);
  CHECK(d.year == 2094);  // two-digit year 94 is read as 2094 by design: receivers in service are 21st century
  CHECK(d.month == 3 && d.day == 23);
  CHECK(d.hour == 12 && d.minute == 35 && d.second == 19);
}

TEST(nmea_rejects_bad_checksum_and_counts_it) {
  NmeaParser p;
  std::string bad = kGga;
  bad[bad.size() - 5] = '0';  // corrupt the checksum digit
  CHECK(feedAll(p, bad) == NMEA_NONE);
  CHECK(!p.data().posValid);
  CHECK(p.data().badChecksum == 1);
  // ...and a good one afterwards still works.
  CHECK(feedAll(p, kGga) == NMEA_GGA);
  CHECK(p.data().posValid);
}

TEST(nmea_rejects_missing_checksum) {
  NmeaParser p;
  CHECK(feedAll(p, "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,\r\n") == NMEA_NONE);
  CHECK(!p.data().posValid);
}

TEST(nmea_no_fix_gga_is_not_a_position) {
  NmeaParser p;
  CHECK(feedAll(p, sentence("GPGGA,123519,,,,,0,00,99.99,,,,,,")) == NMEA_GGA);
  CHECK(!p.data().posValid);
  CHECK(p.data().quality == 0);
  CHECK(p.data().sats == 0);
}

TEST(nmea_accepts_multi_constellation_talker_ids) {
  NmeaParser p;
  CHECK(feedAll(p, sentence("GNGGA,101010.00,3112.0000,N,07422.0000,E,1,09,1.1,210.0,M,0.0,M,,")) == NMEA_GGA);
  CHECK(p.data().posValid);
  CHECK_NEAR(p.data().lat, 31.2, 1e-9);
  CHECK_NEAR(p.data().lon, 74.0 + 22.0 / 60.0, 1e-9);
}

TEST(nmea_southern_and_western_hemispheres) {
  NmeaParser p;
  feedAll(p, sentence("GPGGA,000000,3345.0000,S,15112.0000,W,1,05,1.5,10.0,M,0,M,,"));
  CHECK(p.data().posValid);
  CHECK(p.data().lat < 0 && p.data().lon < 0);
  CHECK_NEAR(p.data().lat, -(33.0 + 45.0 / 60.0), 1e-9);
}

TEST(nmea_rmc_void_status_is_not_time_or_course) {
  NmeaParser p;
  feedAll(p, sentence("GPRMC,123519,V,,,,,,,230394,,"));
  CHECK(!p.data().rmcActive);
  CHECK(!p.data().timeValid);
  CHECK(!p.data().courseValid);
}

TEST(nmea_slow_speed_course_is_not_trusted) {
  NmeaParser p;
  feedAll(p, sentence("GPRMC,123519,A,4807.038,N,01131.000,E,000.1,084.4,230394,,"));
  CHECK(p.data().rmcActive);
  CHECK(!p.data().courseValid);  // 0.05 m/s: course over ground is noise
}

TEST(nmea_survives_garbage_split_input_and_overflow) {
  NmeaParser p;
  // Garbage, a dropped sentence start, then a good sentence delivered byte by byte.
  feedAll(p, "\xFF\xFE garbage ,,, *12\r\n");
  feedAll(p, "GPGGA,truncated no dollar\r\n");
  std::string longJunk = "$" + std::string(300, 'X') + "\r\n";
  feedAll(p, longJunk);
  CHECK(p.data().overflow >= 1);
  CHECK(feedAll(p, kGga) == NMEA_GGA);
  CHECK(p.data().posValid);
}

TEST(nmea_new_dollar_restarts_a_half_received_sentence) {
  NmeaParser p;
  feedAll(p, "$GPGGA,12351");  // line dropped mid-sentence
  CHECK(feedAll(p, kGga) == NMEA_GGA);
  CHECK(p.data().posValid);
}

TEST(nmea_coordinate_validation) {
  double v;
  CHECK(nmeaParseCoord("4807.038", 'N', false, &v));
  CHECK(!nmeaParseCoord("4860.000", 'N', false, &v));   // 60 minutes
  CHECK(!nmeaParseCoord("9100.000", 'N', false, &v));   // 91 degrees
  CHECK(!nmeaParseCoord("18100.000", 'E', true, &v));   // 181 degrees
  CHECK(!nmeaParseCoord("4807.038", 'X', false, &v));   // bad hemisphere
  CHECK(!nmeaParseCoord("", 'N', false, &v));
  CHECK(!nmeaParseCoord("48", 'N', false, &v));
}

TEST(nmea_implausible_position_is_rejected_even_with_valid_checksum) {
  NmeaParser p;
  feedAll(p, sentence("GPGGA,123519,9500.000,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,"));
  CHECK(!p.data().posValid);
}

// ---------------------------------------------------------------- UBX ----

namespace {
void fletcher(const uint8_t* f, size_t len, uint8_t* a, uint8_t* b) {
  *a = *b = 0;
  for (size_t i = 2; i + 2 < len; i++) {
    *a = (uint8_t)(*a + f[i]);
    *b = (uint8_t)(*b + *a);
  }
}
}  // namespace

TEST(ubx_frames_are_well_formed) {
  uint8_t buf[64];
  size_t n1 = ubxCfgPrtUart1(38400, buf, sizeof(buf));
  CHECK(n1 == 28);
  CHECK(buf[0] == 0xB5 && buf[1] == 0x62 && buf[2] == 0x06 && buf[3] == 0x00);
  CHECK(buf[4] == 20 && buf[5] == 0);
  uint8_t a, b;
  fletcher(buf, n1, &a, &b);
  CHECK(buf[n1 - 2] == a && buf[n1 - 1] == b);
  // baud rate 38400 = 0x00009600, little-endian at payload offset 8
  CHECK(buf[6 + 8] == 0x00 && buf[6 + 9] == 0x96 && buf[6 + 10] == 0x00 && buf[6 + 11] == 0x00);
  CHECK(buf[6 + 0] == 1);  // UART1

  size_t n2 = ubxCfgRate(200, buf, sizeof(buf));
  CHECK(n2 == 14);
  CHECK(buf[2] == 0x06 && buf[3] == 0x08 && buf[6] == 200 && buf[7] == 0);
  fletcher(buf, n2, &a, &b);
  CHECK(buf[n2 - 2] == a && buf[n2 - 1] == b);

  size_t n3 = ubxCfgNmeaRate(NMEA_ID_GSV, 0, buf, sizeof(buf));
  CHECK(n3 == 11);
  CHECK(buf[6] == 0xF0 && buf[7] == NMEA_ID_GSV && buf[8] == 0);
  fletcher(buf, n3, &a, &b);
  CHECK(buf[n3 - 2] == a && buf[n3 - 1] == b);
}

TEST(ubx_refuses_to_overrun_the_buffer) {
  uint8_t tiny[10];
  CHECK(ubxCfgPrtUart1(38400, tiny, sizeof(tiny)) == 0);
  CHECK(ubxCfgRate(200, nullptr, 0) == 0);
}
