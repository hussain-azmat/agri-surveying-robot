#pragma once
// Everything that must survive a reboot or an OTA update and is NOT in the repo:
// WiFi credentials, the GitHub token, the rover's access code and the IMU bias.
// They live ONLY in the ESP32's flash (NVS). Nothing in this module prints them, and
// nothing in the firmware writes them to the serial port, the logs or the repo.
#include <Arduino.h>

#include "rt_config.h"

void settingsBegin();

// ---- Identity ------------------------------------------------------------------------------
String roverId();       // "rover-A1B2C3" (from the chip's MAC); names this rover's log folder
String roverMac();      // "A4:CF:12:34:56:78": not a secret; tools/ap_password.py turns it into the label code
String apSsid();        // "RoverSetup-A1B2"
// One access code protects both the rover's own WiFi network and the control page when it is
// reached over a shared network. Derived from the MAC + AP_PASSWORD_SALT (printable on a
// sticker: tools/ap_password.py), or chosen by the owner on the setup page.
String accessCode();
bool accessCodeIsCustom();
void accessCodeSave(const String& code);  // 8..32 printable characters; "" resets to the derived one

// ---- WiFi credentials (client mode) ----------------------------------------------------------
bool wifiCredsPresent();
String wifiSsid();
String wifiPass();
void wifiCredsSave(const String& ssid, const String& pass);
void wifiCredsClear();

// ---- GitHub ----------------------------------------------------------------------------------
String ghOwner();
String ghRepo();
String ghToken();
bool ghTokenPresent();
void ghTokenSave(const String& token);  // fine-grained token, contents read/write, this repo only
void ghRepoSave(const String& owner, const String& repo);

// ---- OTA bookkeeping ---------------------------------------------------------------------------
void otaMarkUpdated(const char* fromVersion);  // call just before the reboot that follows a flash
bool otaJustUpdated(String* fromVersion);       // true on the first boot after an update
void otaClearJustUpdated();

// ---- Misc persistent values ------------------------------------------------------------------
uint32_t bootCountIncrement();
bool imuBiasLoad(float bias[3]);
void imuBiasSave(const float bias[3]);

uint32_t nvsGetU32(const char* key, uint32_t def);
void nvsPutU32(const char* key, uint32_t value);

// ---- Runtime config (config/rover.cfg) ------------------------------------------------------
// Loads the cached copy from flash if there is one (called at boot), clamping every value.
RuntimeConfig runtimeConfigLoad();
// Fetches config/rover.cfg from the repo; if it differs from the cache, validates, caches and
// applies it. Returns true when a NEW config was applied. Only call while the rover is idle.
bool runtimeConfigSync(String* message);
