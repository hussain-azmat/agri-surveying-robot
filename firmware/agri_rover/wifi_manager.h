#pragma once
// WiFi: saved network, setup page, offline mode, plus the rover's identity and flash-stored credentials.
// (Top: the access-code derivation, plain C++, unit-tested. Under #ifdef ARDUINO: everything that touches the radio.)
#include <stddef.h>

//
// The access code protects the rover's own WiFi network and the control page on shared networks.
// It is derived from the chip's MAC and AP_PASSWORD_SALT, so it can be printed on a label without
// storing anything. HONEST LIMIT: anyone who knows the salt (it is in the source) and the MAC (which the
// rover broadcasts) can compute it, so it keeps casual users out; it is not a substitute for choosing
// your own code on the setup page (which replaces it), and the salt should be changed per deployment.

// `macHex12` is the 12 upper-case hex digits of the MAC, no separators. `out` needs 11 bytes.
void deriveAccessCode(const char* salt, const char* macHex12, char* out);

#ifdef ARDUINO
#include <Arduino.h>
#include <WebServer.h>

// ---- Identity and credentials (flash only; never printed, logged or committed) -----------------------
String roverId();       // "rover-A1B2C3" (from the chip's MAC); names this rover's log folder
String roverMac();      // "A4:CF:12:34:56:78": not a secret; tools/ap_password.py turns it into the label code
String apSsid();        // "RoverSetup-A1B2"
// One access code protects both the rover's own WiFi network and the control page on shared networks.
// Derived from the MAC + AP_PASSWORD_SALT (printable on a sticker: tools/ap_password.py), or chosen by the
// owner on the setup page.
String accessCode();
bool accessCodeIsCustom();
void accessCodeSave(const String& code);  // 8..32 characters; "" resets to the derived one

bool wifiCredsPresent();
String wifiSsid();
String wifiPass();
void wifiCredsSave(const String& ssid, const String& pass);
void wifiCredsClear();

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
#endif
