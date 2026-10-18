#include "settings.h"

#include <LittleFS.h>
#include <Preferences.h>
#include <WiFi.h>

#include "accesscode.h"
#include "config.h"
#include "github_api.h"
#include "state.h"

namespace {
Preferences prefs;
bool g_open = false;

String macHex() {
  uint8_t m[6];
  WiFi.macAddress(m);  // works before WiFi.begin(): reads the factory MAC
  char b[13];
  snprintf(b, sizeof(b), "%02X%02X%02X%02X%02X%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
  return String(b);
}

String getStr(const char* key, const char* def = "") {
  if (!g_open) return String(def);
  return prefs.getString(key, def);
}
void putStr(const char* key, const String& v) {
  if (g_open) prefs.putString(key, v);
}

String derivedCode() {
  char out[11];
  deriveAccessCode(AP_PASSWORD_SALT, macHex().c_str(), out);  // pure function, unit-tested; mirrored by tools/ap_password.py
  return String(out);
}

String g_cachedCfgText;
}  // namespace

void settingsBegin() { g_open = prefs.begin("rover", false); }

String roverId() { return "rover-" + macHex().substring(6); }
String roverMac() {
  const String h = macHex();
  String out;
  for (int i = 0; i < 12; i += 2) {
    if (i) out += ':';
    out += h.substring(i, i + 2);
  }
  return out;
}
String apSsid() { return "RoverSetup-" + macHex().substring(8); }

bool accessCodeIsCustom() { return getStr("acode").length() >= 8; }
String accessCode() {
  const String custom = getStr("acode");
  return custom.length() >= 8 ? custom : derivedCode();
}
void accessCodeSave(const String& code) {
  if (code.length() == 0) {
    if (g_open) prefs.remove("acode");
    return;
  }
  if (code.length() < 8 || code.length() > 32) return;
  putStr("acode", code);
}

bool wifiCredsPresent() { return getStr("wssid").length() > 0; }
String wifiSsid() { return getStr("wssid"); }
String wifiPass() { return getStr("wpass"); }
void wifiCredsSave(const String& ssid, const String& pass) {
  putStr("wssid", ssid);
  putStr("wpass", pass);
}
void wifiCredsClear() {
  if (!g_open) return;
  prefs.remove("wssid");
  prefs.remove("wpass");
}

String ghOwner() {
  const String v = getStr("gowner");
  return v.length() ? v : String(GH_OWNER_DEFAULT);
}
String ghRepo() {
  const String v = getStr("grepo");
  return v.length() ? v : String(GH_REPO_DEFAULT);
}
String ghToken() { return getStr("gtoken"); }
bool ghTokenPresent() { return getStr("gtoken").length() > 0; }
void ghTokenSave(const String& token) { putStr("gtoken", token); }
void ghRepoSave(const String& owner, const String& repo) {
  if (owner.length() && repo.length()) {
    putStr("gowner", owner);
    putStr("grepo", repo);
  }
}

void otaMarkUpdated(const char* fromVersion) {
  putStr("ota_from", fromVersion ? fromVersion : "?");
  if (g_open) prefs.putBool("ota_new", true);
}
bool otaJustUpdated(String* fromVersion) {
  if (!g_open || !prefs.getBool("ota_new", false)) return false;
  if (fromVersion) *fromVersion = getStr("ota_from");
  return true;
}
void otaClearJustUpdated() {
  if (g_open) prefs.remove("ota_new");
}

uint32_t bootCountIncrement() {
  if (!g_open) return 0;
  const uint32_t n = prefs.getUInt("boots", 0) + 1;
  prefs.putUInt("boots", n);
  return n;
}

bool imuBiasLoad(float bias[3]) {
  if (!g_open || prefs.getBytesLength("imubias") != sizeof(float) * 3) return false;
  prefs.getBytes("imubias", bias, sizeof(float) * 3);
  for (int i = 0; i < 3; i++) {
    if (!isfinite(bias[i]) || fabsf(bias[i]) > 20.0f) return false;  // implausible: do not trust it
  }
  return true;
}
void imuBiasSave(const float bias[3]) {
  if (g_open) prefs.putBytes("imubias", bias, sizeof(float) * 3);
}

uint32_t nvsGetU32(const char* key, uint32_t def) { return g_open ? prefs.getUInt(key, def) : def; }
void nvsPutU32(const char* key, uint32_t value) {
  if (g_open) prefs.putUInt(key, value);
}

// ------------------------------------------------------------------------------------------------
// Runtime config

static const char* kCfgFile = "/config/rover.cfg";

RuntimeConfig runtimeConfigLoad() {
  RuntimeConfig c;
  File f = LittleFS.open(kCfgFile, "r");
  if (!f) return c;
  const size_t n = f.size();
  if (n == 0 || n > 4096) {  // a config file is a few hundred bytes; anything else is not ours
    f.close();
    return c;
  }
  String text = f.readString();
  f.close();
  parseRtConfig(text.c_str(), &c);
  g_cachedCfgText = text;
  return c;
}

bool runtimeConfigSync(String* message) {
  String dummy;
  if (!message) message = &dummy;
  LittleFS.mkdir("/config");
  const int rc = ghGetToFile(GH_CONFIG_PATH, "/config/rover.cfg.new", 4096);
  if (rc == 404) {
    *message = "no config/rover.cfg in the repo (using defaults)";
    return false;
  }
  if (rc != 200) {
    *message = String("config fetch failed: ") + ghLastError();
    return false;
  }
  File f = LittleFS.open("/config/rover.cfg.new", "r");
  if (!f) {
    *message = "config download vanished";
    return false;
  }
  String text = f.readString();
  f.close();
  if (text == g_cachedCfgText) {
    LittleFS.remove("/config/rover.cfg.new");
    *message = "config unchanged";
    return false;
  }
  RuntimeConfig c;
  const RtConfigReport r = parseRtConfig(text.c_str(), &c);
  if (r.applied == 0 && r.malformed > 0) {  // looks like garbage (e.g. an HTML error page): keep the old one
    LittleFS.remove("/config/rover.cfg.new");
    *message = "downloaded config has no valid settings; ignored";
    return false;
  }
  LittleFS.remove(kCfgFile);
  LittleFS.rename("/config/rover.cfg.new", kCfgFile);
  g_cachedCfgText = text;
  {
    StateLock l;
    g_state.cfg = c;
  }
  *message = String("config applied (") + r.applied + " values, " + r.clamped + " clamped, " + r.ignored + " ignored)";
  return true;
}
