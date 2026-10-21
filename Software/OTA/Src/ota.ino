// =============================================================================
// OTA bench: runs ONLY WiFi + the GitHub update client. Use it to prove the rover can reach GitHub, read
// ota/version.txt and flash a newer image. Serial monitor at 115200. The rover needs a saved WiFi network
// first (use this sketch's setup page, or the WiFi bench).
//
//   When online with a valid clock it CHECKS once and prints the result (it does not install by itself).
//   Type  c  = check again        i  = install the newer version (if there is one)
//
//   {"status":"INFO","message":"update check: ..."}
//   {"event":"OTA_START","detail":"0.3.0"}  ...  {"event":"OTA_DONE","detail":"0.3.0"}   then the rover reboots
//
// PASS (see Software/OTA/Readme.txt): "up to date" when the repo has the same version, "available" when it is
// newer, and an install ends in a reboot that reports the new version. The repo is public, so no token is needed.
// =============================================================================
#include "config.h"
#include "github_log.h"
#include "motors.h"
#include "ota_github.h"
#include "remote.h"
#include "safety.h"
#include "wifi_manager.h"

static bool g_checkedOnce = false;
static uint32_t g_lastPrintMs = 0;

static void check(bool install) {
  String msg;
  otaCheckNow(&msg, install);
  serialLog("INFO", "update check: %s", msg.c_str());
}

void setup() {
  motorsBegin();
  Serial.begin(115200);
  delay(300);
  serialLog("INFO", "OTA bench v" FW_VERSION ": c = check, i = install a newer version");
  stateBegin();
  otaBegin();
  wifiBegin(setupButtonHeldAtBoot());
  remoteBegin();
}

void loop() {
  wifiUpdate();
  remoteUpdate();
  const bool online = wifiOnline() && wifiTimeSynced();
  if (online && !g_checkedOnce) {
    g_checkedOnce = true;
    check(false);
  }
  while (Serial.available()) {
    const int c = Serial.read();
    if (c == 'c' || c == 'C') check(false);
    if (c == 'i' || c == 'I') check(true);
  }
  if (millis() - g_lastPrintMs >= 5000) {
    g_lastPrintMs = millis();
    if (!online) serialLog("INFO", "waiting: wifi %s, clock %s", wifiStateName(), wifiTimeSynced() ? "set" : "not set");
  }
  delay(2);
}
