#include "gps.h"

#include <HardwareSerial.h>
#include <sys/time.h>
#include <time.h>

#include "ubx.h"

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
  Serial.println("[gps] started, listening for NEO-6M");
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
    Serial.println("[gps] NEO-6M configured: 38400 baud, 5 Hz, GGA+RMC");
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
  Serial.println("[gps] system clock set from GPS");
}
