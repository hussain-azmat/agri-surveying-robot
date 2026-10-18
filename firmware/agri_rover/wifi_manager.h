#pragma once
// WiFi: saved network, setup page, offline mode. Nothing here ever blocks the main loop for more
// than a few milliseconds, and the control task does not depend on WiFi at all.
//
//   boot -> saved credentials? -> CONNECTING (15 s, non-blocking) -> ONLINE
//        -> none, or it failed  -> SETUP: the rover's own WiFi "RoverSetup-xxxx" (password-protected)
//                                   with a page that lists nearby networks. You pick one and enter
//                                   its password; the rover TESTS the connection and only then
//                                   saves it to flash.
//        -> nobody finishes setup in 4 minutes -> OFFLINE: the rover's own WiFi stays up, so driving
//                                   from a phone still works; OTA and log uploads simply do not run.
//   Re-enter setup: hold the button ~5 s at power-up, or use "Change WiFi" on the control page.
//   Credentials live only in flash. They are never printed, logged or committed.
#include <Arduino.h>
#include <WebServer.h>

enum class NetState : uint8_t { BOOT, CONNECTING, ONLINE, PORTAL, OFFLINE_AP };

void wifiBegin(bool forcePortal);
void wifiUpdate();  // call every loop iteration

NetState wifiState();
const char* wifiStateName();
bool wifiOnline();       // connected to a network that gives us the internet path (STA up)
bool wifiApActive();     // the rover's own network is up
String wifiIp();         // STA address when online, otherwise the AP address
String wifiHost();       // "rover-a1b2c3.local" (mDNS) when online
bool wifiTimeSynced();   // system clock is plausible (NTP or GPS)
void wifiRequestPortal();  // "Change WiFi"

// Adds /setup, /api/setup/* and the captive-portal probe URLs to the shared web server.
void wifiRegisterRoutes(WebServer& server);

// True if the setup button is held for SETUP_BTN_HOLD_MS right now. Blocks while it is held, so
// only call it from setup(). The LED blinks while counting.
bool setupButtonHeldAtBoot();
