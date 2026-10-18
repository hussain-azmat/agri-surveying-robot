#include "gps.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace {

int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

// Splits `s` in place on commas. Returns the number of fields (max `maxFields`).
int splitFields(char* s, char** fields, int maxFields) {
  int n = 0;
  fields[n++] = s;
  for (char* p = s; *p && n < maxFields; p++) {
    if (*p == ',') {
      *p = '\0';
      fields[n++] = p + 1;
    }
  }
  return n;
}

bool parseUInt(const char* s, int digits, int* out) {
  int v = 0;
  for (int i = 0; i < digits; i++) {
    if (s[i] < '0' || s[i] > '9') return false;
    v = v * 10 + (s[i] - '0');
  }
  *out = v;
  return true;
}

// hhmmss(.sss)
bool parseTime(const char* f, uint8_t* h, uint8_t* m, uint8_t* s) {
  if (strlen(f) < 6) return false;
  int hh, mm, ss;
  if (!parseUInt(f, 2, &hh) || !parseUInt(f + 2, 2, &mm) || !parseUInt(f + 4, 2, &ss)) return false;
  if (hh > 23 || mm > 59 || ss > 60) return false;
  *h = (uint8_t)hh;
  *m = (uint8_t)mm;
  *s = (uint8_t)(ss > 59 ? 59 : ss);
  return true;
}

// ddmmyy
bool parseDate(const char* f, uint16_t* y, uint8_t* mo, uint8_t* d) {
  if (strlen(f) < 6) return false;
  int dd, mm, yy;
  if (!parseUInt(f, 2, &dd) || !parseUInt(f + 2, 2, &mm) || !parseUInt(f + 4, 2, &yy)) return false;
  if (dd < 1 || dd > 31 || mm < 1 || mm > 12) return false;
  *y = (uint16_t)(2000 + yy);
  *mo = (uint8_t)mm;
  *d = (uint8_t)dd;
  return true;
}

bool endsWith3(const char* s, const char* tail) {
  size_t n = strlen(s);
  return n >= 3 && memcmp(s + n - 3, tail, 3) == 0;
}

}  // namespace

bool nmeaChecksumOk(const char* sentence, size_t len, const char* hex) {
  if (!hex || hexVal(hex[0]) < 0 || hexVal(hex[1]) < 0) return false;
  uint8_t x = 0;
  for (size_t i = 0; i < len; i++) x ^= (uint8_t)sentence[i];
  return x == (uint8_t)((hexVal(hex[0]) << 4) | hexVal(hex[1]));
}

bool nmeaParseCoord(const char* field, char hemi, bool isLon, double* out) {
  if (!field || !*field) return false;
  const int degDigits = isLon ? 3 : 2;
  size_t n = strlen(field);
  if (n < (size_t)degDigits + 3) return false;
  int deg;
  if (!parseUInt(field, degDigits, &deg)) return false;
  char* end = nullptr;
  double minutes = strtod(field + degDigits, &end);
  if (end == field + degDigits || !isfinite(minutes) || minutes < 0 || minutes >= 60.0) return false;
  double v = deg + minutes / 60.0;
  if (hemi == 'S' || hemi == 'W') v = -v;
  else if (hemi != 'N' && hemi != 'E') return false;
  const double limit = isLon ? 180.0 : 90.0;
  if (v < -limit || v > limit) return false;
  *out = v;
  return true;
}

void NmeaParser::reset() {
  d_ = NmeaData();
  len_ = 0;
  in_ = false;
  overflowed_ = false;
}

uint8_t NmeaParser::feed(char c) {
  if (c == '$') {  // a new sentence always restarts, even mid-garbage
    in_ = true;
    len_ = 0;
    overflowed_ = false;
    return NMEA_NONE;
  }
  if (!in_) return NMEA_NONE;
  if (c == '\r' || c == '\n') {
    uint8_t ev = NMEA_NONE;
    if (!overflowed_ && len_ > 0) {
      buf_[len_] = '\0';
      ev = handle();
    }
    in_ = false;
    len_ = 0;
    return ev;
  }
  if (len_ >= sizeof(buf_) - 1) {
    overflowed_ = true;  // too long to be a real sentence: drop it
    d_.overflow++;
    in_ = false;
    return NMEA_NONE;
  }
  buf_[len_++] = c;
  return NMEA_NONE;
}

uint8_t NmeaParser::handle() {
  // buf_ = "GPGGA,....*hh"
  char* star = strrchr(buf_, '*');
  if (!star || strlen(star) < 3) {
    d_.badChecksum++;
    return NMEA_NONE;
  }
  if (!nmeaChecksumOk(buf_, (size_t)(star - buf_), star + 1)) {
    d_.badChecksum++;
    return NMEA_NONE;
  }
  *star = '\0';
  d_.sentences++;

  char* f[20];
  int n = splitFields(buf_, f, 20);
  if (n < 1) return NMEA_NONE;

  if (endsWith3(f[0], "GGA") && n >= 10) {
    d_.ggaCount++;
    d_.haveGga = true;
    d_.quality = (uint8_t)atoi(f[6]);
    d_.sats = (uint8_t)atoi(f[7]);
    d_.hdop = *f[8] ? (float)atof(f[8]) : 99.9f;
    d_.altM = *f[9] ? (float)atof(f[9]) : 0.0f;
    double lat, lon;
    d_.posValid = d_.quality > 0 && nmeaParseCoord(f[2], f[3][0], false, &lat) &&
                  nmeaParseCoord(f[4], f[5][0], true, &lon);
    if (d_.posValid) {
      d_.lat = lat;
      d_.lon = lon;
    }
    return NMEA_GGA;
  }

  if (endsWith3(f[0], "RMC") && n >= 10) {
    d_.rmcCount++;
    d_.haveRmc = true;
    d_.rmcActive = f[2][0] == 'A';
    d_.speedMps = *f[7] ? (float)(atof(f[7]) * 0.514444) : 0.0f;
    const bool haveCourse = *f[8] != '\0';
    d_.courseDeg = haveCourse ? (float)atof(f[8]) : 0.0f;
    d_.courseValid = d_.rmcActive && haveCourse && d_.speedMps >= 0.3f;
    uint8_t h, m, s, mo, d;
    uint16_t y;
    d_.timeValid = d_.rmcActive && parseTime(f[1], &h, &m, &s) && parseDate(f[9], &y, &mo, &d);
    if (d_.timeValid) {
      d_.hour = h;
      d_.minute = m;
      d_.second = s;
      d_.year = y;
      d_.month = mo;
      d_.day = d;
    }
    return NMEA_RMC;
  }
  return NMEA_NONE;
}

size_t ubxBuild(uint8_t cls, uint8_t id, const uint8_t* payload, uint16_t len, uint8_t* out, size_t cap) {
  const size_t total = (size_t)len + 8;
  if (!out || cap < total) return 0;
  out[0] = 0xB5;
  out[1] = 0x62;
  out[2] = cls;
  out[3] = id;
  out[4] = (uint8_t)(len & 0xFF);
  out[5] = (uint8_t)(len >> 8);
  for (uint16_t i = 0; i < len; i++) out[6 + i] = payload ? payload[i] : 0;
  uint8_t a = 0, b = 0;  // Fletcher-8 over class..payload
  for (size_t i = 2; i < 6 + (size_t)len; i++) {
    a = (uint8_t)(a + out[i]);
    b = (uint8_t)(b + a);
  }
  out[6 + len] = a;
  out[7 + len] = b;
  return total;
}

size_t ubxCfgPrtUart1(uint32_t baud, uint8_t* out, size_t cap) {
  uint8_t p[20] = {0};
  p[0] = 1;  // portID: UART1
  // mode: 8 data bits (3<<6), no parity (4<<9), 1 stop bit (0<<12) = 0x000008D0
  p[4] = 0xD0;
  p[5] = 0x08;
  p[8] = (uint8_t)(baud & 0xFF);
  p[9] = (uint8_t)((baud >> 8) & 0xFF);
  p[10] = (uint8_t)((baud >> 16) & 0xFF);
  p[11] = (uint8_t)((baud >> 24) & 0xFF);
  p[12] = 0x03;  // inProtoMask: UBX + NMEA
  p[14] = 0x02;  // outProtoMask: NMEA only (smaller, and what our parser reads)
  return ubxBuild(0x06, 0x00, p, sizeof(p), out, cap);
}

size_t ubxCfgRate(uint16_t measMs, uint8_t* out, size_t cap) {
  uint8_t p[6];
  p[0] = (uint8_t)(measMs & 0xFF);
  p[1] = (uint8_t)(measMs >> 8);
  p[2] = 1;  // navRate: every measurement
  p[3] = 0;
  p[4] = 1;  // timeRef: GPS time
  p[5] = 0;
  return ubxBuild(0x06, 0x08, p, sizeof(p), out, cap);
}

size_t ubxCfgNmeaRate(uint8_t nmeaId, uint8_t rate, uint8_t* out, size_t cap) {
  const uint8_t p[3] = {0xF0, nmeaId, rate};  // class 0xF0 = standard NMEA messages
  return ubxBuild(0x06, 0x01, p, sizeof(p), out, cap);
}

#ifdef ARDUINO
#include <HardwareSerial.h>
#include <sys/time.h>
#include <time.h>
#include "github_log.h"
#include "safety.h"

namespace {

HardwareSerial gpsSerial(1);
NmeaParser parser;
GpsData g;

uint32_t g_baud = GPS_BAUD_TARGET;
uint32_t g_lastByteMs = 0, g_lastValidMs = 0, g_lastGgaMs = 0, g_baudSwitchMs = 0;
uint32_t g_lastSentences = 0;

// Configuration sequence (non-blocking): 0 = not needed, 1 = PRT sent (wait), 2 = send the rest.
int g_cfgStage = 0;
uint32_t g_cfgStageMs = 0;
bool g_configured = false;  // true once we have seen valid NMEA at the target baud

// Measured GGA rate over a sliding window
uint32_t g_rateWindowStart = 0, g_rateGga = 0;

void sendFrame(size_t n, const uint8_t* f) { gpsSerial.write(f, n); }

void sendConfigBurst() {
  uint8_t buf[32];
  size_t n = ubxCfgRate(GPS_RATE_MS, buf, sizeof(buf));
  sendFrame(n, buf);
  // Only GGA (position, quality, sats, HDOP) and RMC (speed, course, time) are used.
  const uint8_t off[] = {NMEA_ID_GLL, NMEA_ID_GSA, NMEA_ID_GSV, NMEA_ID_VTG};
  for (uint8_t id : off) {
    n = ubxCfgNmeaRate(id, 0, buf, sizeof(buf));
    sendFrame(n, buf);
    delay(2);
  }
  n = ubxCfgNmeaRate(NMEA_ID_GGA, 1, buf, sizeof(buf));
  sendFrame(n, buf);
  n = ubxCfgNmeaRate(NMEA_ID_RMC, 1, buf, sizeof(buf));
  sendFrame(n, buf);
  gpsSerial.flush();
}

void switchBaud(uint32_t baud) {
  g_baud = baud;
  gpsSerial.updateBaudRate(baud);
  g_baudSwitchMs = millis();
  parser.reset();
  g.baud = baud;
}

}  // namespace

void gpsBegin() {
  gpsSerial.setRxBufferSize(1024);  // survives the control task being delayed by a flash write
  gpsSerial.begin(GPS_BAUD_TARGET, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
  g_baud = GPS_BAUD_TARGET;
  g.baud = g_baud;
  g_baudSwitchMs = g_lastByteMs = millis();
  serialLog("INFO", "GPS started on RX=GPIO%d TX=GPIO%d, listening for the NEO-6M", PIN_GPS_RX, PIN_GPS_TX);
}

uint8_t gpsUpdate() {
  const uint32_t now = millis();
  uint8_t events = 0;
  int budget = 400;  // never spend more than a slice of one control tick draining the UART
  while (gpsSerial.available() > 0 && budget-- > 0) {
    const char c = (char)gpsSerial.read();
    g_lastByteMs = now;
    events |= parser.feed(c);
  }
  const NmeaData& d = parser.data();

  if (d.sentences != g_lastSentences) {  // a sentence with a good checksum arrived
    g_lastSentences = d.sentences;
    g_lastValidMs = now;
  }

  if (events & NMEA_GGA) {
    g_lastGgaMs = now;
    g.fixCount++;
    g_rateGga++;
    // Seeing valid NMEA at the target baud means the module is talking our language.
    if (g_baud == GPS_BAUD_TARGET) g_configured = true;
  }

  // ---- Bring-up state machine ----
  const bool silent = now - g_lastValidMs > 3000 && now - g_baudSwitchMs > 3000;
  if (silent) {
    g_configured = false;
    g_cfgStage = 0;
    switchBaud(g_baud == GPS_BAUD_TARGET ? GPS_BAUD_DEFAULT : GPS_BAUD_TARGET);  // try the other speed
    g_lastValidMs = now;  // restart the 3 s window at the new speed
  }
  if (g_baud == GPS_BAUD_DEFAULT && (events & (NMEA_GGA | NMEA_RMC)) && g_cfgStage == 0) {
    // Hearing it at 9600: tell it to switch to 38400 (it replies on the new speed from now on).
    uint8_t buf[32];
    const size_t n = ubxCfgPrtUart1(GPS_BAUD_TARGET, buf, sizeof(buf));
    sendFrame(n, buf);
    gpsSerial.flush();
    g_cfgStage = 1;
    g_cfgStageMs = now;
  } else if (g_cfgStage == 1 && now - g_cfgStageMs > 150) {
    switchBaud(GPS_BAUD_TARGET);
    g_cfgStage = 2;
    g_cfgStageMs = now;
  } else if (g_cfgStage == 2 && now - g_cfgStageMs > 100) {
    sendConfigBurst();
    g_cfgStage = 0;
    serialLog("INFO", "NEO-6M configured: 38400 baud, 5 Hz, GGA+RMC");
  }
  // At the target baud but still hearing 1 Hz or full sentence sets (e.g. module kept 38400 but
  // lost its rate settings after a reset): re-apply the burst once the measured rate is wrong.
  if (g_baud == GPS_BAUD_TARGET && g_configured && g_cfgStage == 0 && now - g_rateWindowStart >= 5000) {
    const float hz = g_rateGga / ((now - g_rateWindowStart) / 1000.0f);
    g.rateHz = hz;
    if (g_rateWindowStart != 0 && hz < 3.5f && g.healthy) sendConfigBurst();
    g_rateWindowStart = now;
    g_rateGga = 0;
  }

  // ---- Publish a snapshot ----
  g.healthy = now - g_lastValidMs < 3000;
  g.baud = g_baud;
  g.badChecksum = d.badChecksum;
  g.fixAgeMs = g_lastGgaMs ? now - g_lastGgaMs : 0xFFFFFFFF;
  g.quality = d.quality;
  g.sats = d.sats;
  g.hdop = d.hdop;
  g.altM = d.altM;
  g.fix = d.posValid && g.fixAgeMs <= GPS_MAX_AGE_MS;
  if (d.posValid) {
    g.lat = d.lat;
    g.lon = d.lon;
  }
  g.speedMps = d.speedMps;
  g.courseDeg = d.courseDeg;
  g.courseValid = d.courseValid && d.rmcActive;
  g.timeValid = d.timeValid;
  g.year = d.year;
  g.month = d.month;
  g.day = d.day;
  g.hour = d.hour;
  g.minute = d.minute;
  g.second = d.second;
  return events;
}

GpsData gpsGet() { return g; }

void gpsSyncSystemClock() {
  const time_t now = time(nullptr);
  if (now > 1704067200) return;  // already set (after 2024-01-01) by NTP or an earlier call
  GpsData c;
  {
    StateLock l;
    c = g_state.gps;
  }
  if (!c.timeValid || c.year < 2024) return;
  struct tm t = {};
  t.tm_year = c.year - 1900;
  t.tm_mon = c.month - 1;
  t.tm_mday = c.day;
  t.tm_hour = c.hour;
  t.tm_min = c.minute;
  t.tm_sec = c.second;
  // The ESP32's timezone is UTC unless set, so mktime() yields UTC seconds.
  const time_t epoch = mktime(&t);
  if (epoch < 1704067200) return;
  struct timeval tv = {epoch, 0};
  settimeofday(&tv, nullptr);
  serialLog("INFO", "system clock set from the GPS");
}
#endif
