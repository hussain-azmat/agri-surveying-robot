#pragma once
// GPS: u-blox NEO-6M on a UART.
//   top half  : NMEA parser, u-blox UBX frame builders, GpsData. Plain C++, unit-tested on a laptop.
//   under #ifdef ARDUINO: the UART driver (bring-up, auto-baud, 5 Hz, only GGA+RMC).
#include <stddef.h>
#include <stdint.h>

//
// Streaming NMEA-0183 parser for the u-blox NEO-6M. Feed it one byte at a time from
// the UART; it validates the checksum and extracts only what the rover needs:
//   GGA -> position, fix quality, satellites, HDOP, altitude
//   RMC -> speed, course over ground, UTC date/time

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

//
// The NEO-6M boots at 9600 baud / 1 Hz with a full set of NMEA sentences. 5 Hz of that
// does not fit through 9600 baud, so at start-up we (1) raise the baud rate, (2) set the
// measurement rate to 200 ms, (3) switch off the sentences we do not use (GLL, GSA, GSV,
// VTG), keeping GGA + RMC. Nothing is saved to the module's flash: it is re-applied on
// every boot, so a replaced or reset module needs no manual setup.

// Generic frame: sync, class, id, little-endian length, payload, Fletcher checksum.
// Returns the frame length, or 0 if `cap` is too small.
size_t ubxBuild(uint8_t cls, uint8_t id, const uint8_t* payload, uint16_t len, uint8_t* out, size_t cap);

// UBX-CFG-PRT for UART1: 8N1, UBX+NMEA in, NMEA out, at `baud`.
size_t ubxCfgPrtUart1(uint32_t baud, uint8_t* out, size_t cap);
// UBX-CFG-RATE: measurement period in ms (200 = 5 Hz), 1 measurement per navigation solution.
size_t ubxCfgRate(uint16_t measMs, uint8_t* out, size_t cap);
// UBX-CFG-MSG: output rate (0 = off, 1 = every solution) of one NMEA sentence on the current port.
// nmeaId: 0x00 GGA, 0x01 GLL, 0x02 GSA, 0x03 GSV, 0x04 RMC, 0x05 VTG
size_t ubxCfgNmeaRate(uint8_t nmeaId, uint8_t rate, uint8_t* out, size_t cap);

constexpr uint8_t NMEA_ID_GGA = 0x00, NMEA_ID_GLL = 0x01, NMEA_ID_GSA = 0x02, NMEA_ID_GSV = 0x03,
                  NMEA_ID_RMC = 0x04, NMEA_ID_VTG = 0x05;

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

#ifdef ARDUINO
#include <Arduino.h>

// u-blox NEO-6M on a UART: bring-up (auto-baud, 5 Hz, only GGA+RMC) and NMEA parsing.
// Called ONLY from the control task (one owner of the UART), apart from gpsBegin().
//
// Bring-up is non-blocking and self-healing. The module's power-on default is 9600 baud / 1 Hz,
// but a module that stayed powered (backup battery) may still be at 38400 from the last boot, so
// the driver listens at one speed and, after 3 s of silence, tries the other. When it hears valid
// NMEA at 9600 it configures the module (38400 baud, 200 ms rate, GGA+RMC only). A GPS that is
// unplugged and reconnected, or reset, is found again the same way. The receiver's own flash is
// never written.


void gpsBegin();
// Drain the UART and parse. Returns a bitmask of NmeaEvent for sentences completed in this call.
uint8_t gpsUpdate();
// Snapshot of the latest data (valid fix means: a position, fresh within GPS_MAX_AGE_MS).
GpsData gpsGet();
// Sets the ESP32 system clock from GPS UTC when WiFi/NTP has not (so log rows and file names get
// real dates in the field). Safe to call from any task; does nothing if already set.
void gpsSyncSystemClock();
#endif
