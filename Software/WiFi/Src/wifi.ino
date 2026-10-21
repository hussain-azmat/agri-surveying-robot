// =============================================================================
// WiFi bench: runs ONLY WiFi + the setup page (plus the web server it needs). Use it to check the setup flow
// without the rest of the rover. Serial monitor at 115200.
//
//   no saved network  -> the rover opens its own network "RoverSetup-xxxx" (password = the access code,
//                        python tools/ap_password.py <MAC>), join it, any web page shows the setup page.
//   saved network     -> connects; the serial line shows state ONLINE and the IP address.
//   {"status":"INFO","message":"wifi state ONLINE, ip 192.168.1.50, clock set"}   every 2 s
//
// PASS (see Software/WiFi/Readme.txt): the setup flow works, a WRONG password is reported and saves nothing,
// the right one connects, and after a reboot it reconnects by itself. Hold the setup button (GPIO 15 to GND)
// ~5 s at power-up to force the setup page again.
// =============================================================================
#include "config.h"
#include "github_log.h"
#include "motors.h"
#include "remote.h"
#include "safety.h"
#include "wifi_manager.h"

static uint32_t g_lastPrintMs = 0;

void setup() {
  motorsBegin();  // even on a bench: drivers off first
  Serial.begin(115200);
  delay(300);
  serialLog("INFO", "WiFi bench v" FW_VERSION ": id %s, MAC %s", roverId().c_str(), roverMac().c_str());
  stateBegin();
  const bool forceSetup = setupButtonHeldAtBoot();
  wifiBegin(forceSetup);
  remoteBegin();
}

void loop() {
  wifiUpdate();
  remoteUpdate();
  if (millis() - g_lastPrintMs >= 2000) {
    g_lastPrintMs = millis();
    serialLog("INFO", "wifi state %s, ip %s, clock %s", wifiStateName(), wifiIp().c_str(), wifiTimeSynced() ? "set" : "not set");
  }
  delay(2);
}
