// PURE: no Arduino dependencies.
#include "nmea.h"

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
