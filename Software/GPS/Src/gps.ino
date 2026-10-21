// =============================================================================
// GPS bench: runs ONLY the u-blox NEO-6M driver. Use it to check the antenna, the wiring (GPS TX -> GPIO 34,
// GPS RX <- GPIO 17) and the 5 Hz configuration. Take the rover (or just the GPS) OUTSIDE with a clear sky.
// Serial monitor at 115200:
//
//   {"gps":{"fix":true,"quality":1,"sats":9,"hdop":1.1,"lat":...,"lon":...,"speed_mps":0.12,"course_deg":84.4}}  every 1 s
//   {"gps_link":{"healthy":true,"baud":38400,"rate_hz":5.0,"bad_checksum":0,"fixes":123}}
//
// PASS (see Software/GPS/Readme.txt): healthy=true and baud=38400 within ~10 s (the driver switches the module),
// rate_hz about 5.0, a fix within a few minutes outdoors, hdop below 3, sats 6 or more, lat/lon where you are.
// =============================================================================
#include "config.h"
#include "github_log.h"
#include "gps.h"

static uint32_t g_lastPrintMs = 0;

void setup() {
  Serial.begin(115200);
  delay(300);
  serialLog("INFO", "GPS bench v" FW_VERSION ": only the GPS runs. Go outside with a clear sky; a first fix can take minutes.");
  gpsBegin();
}

void loop() {
  gpsUpdate();
  if (millis() - g_lastPrintMs >= 1000) {
    g_lastPrintMs = millis();
    const GpsData g = gpsGet();
    char line[320];
    formatGpsJson(line, sizeof(line), g);
    Serial.println(line);
    snprintf(line, sizeof(line),
             "{\"gps_link\":{\"healthy\":%s,\"baud\":%lu,\"rate_hz\":%.1f,\"bad_checksum\":%lu,\"fixes\":%lu,\"fix_age_ms\":%ld}}",
             g.healthy ? "true" : "false", (unsigned long)g.baud, g.rateHz, (unsigned long)g.badChecksum,
             (unsigned long)g.fixCount, g.fixAgeMs == 0xFFFFFFFF ? -1L : (long)g.fixAgeMs);
    Serial.println(line);
  }
  delay(2);
}
