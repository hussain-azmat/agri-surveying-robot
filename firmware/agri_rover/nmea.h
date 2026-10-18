#pragma once
// PURE: no Arduino dependencies. Unit-tested on a laptop (test/test_nmea.cpp).
//
// Streaming NMEA-0183 parser for the u-blox NEO-6M. Feed it one byte at a time from
// the UART; it validates the checksum and extracts only what the rover needs:
//   GGA -> position, fix quality, satellites, HDOP, altitude
//   RMC -> speed, course over ground, UTC date/time
#include <stddef.h>
#include <stdint.h>

struct NmeaData {
  // From GGA
  bool haveGga = false;
  uint8_t quality = 0;  // 0 = no fix, 1 = GPS, 2 = DGPS, ...
  uint8_t sats = 0;
  float hdop = 99.9f;
  float altM = 0;
  bool posValid = false;  // true only when the last GGA had quality > 0 and a plausible position
  double lat = 0, lon = 0;
  // From RMC
  bool haveRmc = false;
  bool rmcActive = false;  // status 'A'
  float speedMps = 0;
  float courseDeg = 0;
  bool courseValid = false;  // active, moving (>= 0.3 m/s) and the field was present
  bool timeValid = false;    // UTC date and time both present and plausible
  uint16_t year = 0;
  uint8_t month = 0, day = 0, hour = 0, minute = 0, second = 0;
  // Health counters
  uint32_t sentences = 0, ggaCount = 0, rmcCount = 0, badChecksum = 0, overflow = 0;
};

enum NmeaEvent : uint8_t { NMEA_NONE = 0, NMEA_GGA = 1, NMEA_RMC = 2 };

class NmeaParser {
 public:
  // Returns a bitmask of NmeaEvent for the sentence completed by this byte (usually 0).
  uint8_t feed(char c);
  const NmeaData& data() const { return d_; }
  void reset();

 private:
  uint8_t handle();
  NmeaData d_;
  char buf_[100];
  uint8_t len_ = 0;
  bool in_ = false;
  bool overflowed_ = false;
};

// Helpers exposed for testing.
// Parses ddmm.mmmm (lat) / dddmm.mmmm (lon) plus hemisphere into signed degrees.
bool nmeaParseCoord(const char* field, char hemi, bool isLon, double* out);
// True when `sentence` (between '$' and '*', exclusive) matches the two hex digits in `hex`.
bool nmeaChecksumOk(const char* sentence, size_t len, const char* hex);
