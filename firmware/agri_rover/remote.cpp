#include "remote.h"

#ifdef ARDUINO
#include <ArduinoJson.h>
#include <WiFi.h>
#include "config.h"
#include "github_log.h"
#include "imu.h"
#include "ota_github.h"
#include "path.h"
#include "safety.h"
#include "ui.h"
#include "wifi_manager.h"

namespace {

WebServer server(80);

uint8_t g_authFails = 0;
uint32_t g_lockUntilMs = 0;

bool constEq(const String& a, const String& b) {
  if (a.length() != b.length()) return false;
  uint8_t diff = 0;
  for (size_t i = 0; i < a.length(); i++) diff |= (uint8_t)(a[i] ^ b[i]);
  return diff == 0;
}

void sendJson(int code, const String& body) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", body);
}

void reply(bool ok, const String& msg, int code = 0) {
  JsonDocument d;
  d["ok"] = ok;
  d["msg"] = msg;
  String o;
  serializeJson(d, o);
  sendJson(code ? code : (ok ? 200 : 409), o);
}

// Common guard: authorised requests also count as a heartbeat from the page.
bool guard() {
  if (!remoteAuthorized(server)) {
    remoteDenied(server);
    return false;
  }
  stateHeartbeat();
  return true;
}

bool isIdle(const RoverState& s) { return s.mode == Mode::DISARMED; }

String statusJson() {
  const RoverState s = stateSnapshot();
  const LogStatus ls = logStatus();
  const OtaStatus os = otaStatus();
  JsonDocument d;
  d["id"] = roverId();
  d["ver"] = FW_VERSION;
  d["net"] = wifiStateName();
  d["ip"] = wifiIp();
  d["host"] = wifiHost();
  d["clock"] = wifiTimeSynced();
  d["mode"] = s.mode == Mode::MANUAL ? "MANUAL" : (s.mode == Mode::AUTO ? "AUTO" : "DISARMED");
  d["reason"] = s.stopReason;
  d["linkMs"] = (uint32_t)(millis() - s.lastCmdMs);

  JsonObject g = d["gps"].to<JsonObject>();
  g["ok"] = s.gps.healthy;
  g["fix"] = s.gps.fix;
  g["q"] = s.gps.quality;
  g["sats"] = s.gps.sats;
  g["hdop"] = serialized(String(s.gps.hdop, 1));
  if (s.gps.fix) {
    g["lat"] = serialized(String(s.gps.lat, 7));
    g["lon"] = serialized(String(s.gps.lon, 7));
  }
  g["spd"] = serialized(String(s.gps.speedMps, 2));
  g["hz"] = serialized(String(s.gps.rateHz, 1));
  g["baud"] = s.gps.baud;

  JsonObject i = d["imu"].to<JsonObject>();
  i["ok"] = s.imu.ok;
  i["cal"] = s.imu.calibrated;
  i["calibrating"] = s.imu.calibrating;
  i["yawOk"] = s.imu.yawValid;
  i["yaw"] = serialized(String(s.imu.yawDeg, 0));
  i["tilt"] = serialized(String(s.imu.tiltDeg, 0));
  i["noise"] = serialized(String(s.imu.gyroNoiseDps, 2));

  if (s.battKnown) d["batt"] = serialized(String(s.battV, 2));

  JsonObject f = d["follow"].to<JsonObject>();
  f["state"] = followStateName(s.follow.state);
  f["wp"] = s.follow.wpIndex;
  f["n"] = s.follow.wpCount;
  f["xte"] = serialized(String(s.follow.xteM, 1));
  f["dist"] = serialized(String(s.follow.distToWpM, 1));

  d["rec"] = s.recording;
  d["recN"] = s.recPoints;
  d["path"] = pathLoadedName();

  JsonObject l = d["log"].to<JsonObject>();
  l["bytes"] = ls.pendingBytes;
  l["files"] = ls.queuedFiles;
  l["rows"] = ls.rowsLogged;
  l["full"] = ls.full;
  l["err"] = ls.lastError;
  l["tok"] = ghTokenPresent();

  JsonObject o = d["ota"].to<JsonObject>();
  o["state"] = os.state;
  o["msg"] = os.message;
  o["pct"] = os.percent;
  o["remote"] = os.remoteVersion;
  o["avail"] = os.available;
  o["auto"] = os.autoUpdate;

  String from;
  if (otaJustUpdated(&from)) d["updatedFrom"] = from;

  JsonArray w = d["wheels"].to<JsonArray>();
  for (int k = 0; k < W_COUNT; k++) w.add(serialized(String(s.wheel[k], 2)));

  String out;
  serializeJson(d, out);
  return out;
}

void registerRoutes() {
  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html", UI_CONTROL_HTML); });
  server.on("/api/ping", HTTP_GET, []() { sendJson(200, String("{\"ok\":true,\"id\":\"") + roverId() + "\"}"); });

  server.on("/api/status", HTTP_GET, []() {
    if (!guard()) return;
    sendJson(200, statusJson());
  });

  // ---- Arming and driving -------------------------------------------------------------------
  server.on("/api/arm", HTTP_POST, []() {
    if (!guard()) return;
    const char* why = "";
    if (stateArm(&why)) {
      otaClearJustUpdated();  // the update banner has been seen
      reply(true, "armed");
    } else {
      reply(false, String("not armed: ") + why);
    }
  });
  server.on("/api/disarm", HTTP_POST, []() {
    if (!guard()) return;
    stateDisarm("USER_DISARM");
    pathAutoStop();
    reply(true, "disarmed");
  });
  server.on("/api/stop", HTTP_POST, []() {  // the big red button: stop NOW and disarm
    if (!guard()) return;
    stateDisarm("USER_STOP");
    pathAutoStop();
    reply(true, "stopped");
  });
  server.on("/api/drive", HTTP_POST, []() {
    if (!guard()) return;
    if (stateSnapshot().mode != Mode::MANUAL) return reply(false, "not in manual mode");
    stateDrive(server.arg("t").toFloat(), server.arg("s").toFloat());
    server.send(204);
  });
  server.on("/api/hb", HTTP_POST, []() {
    if (!guard()) return;
    server.send(204);
  });

  // ---- Paths ---------------------------------------------------------------------------------
  server.on("/api/rec/start", HTTP_POST, []() {
    if (!guard()) return;
    const char* why = "";
    if (pathRecStart(&why)) reply(true, "recording");
    else reply(false, why);
  });
  server.on("/api/rec/stop", HTTP_POST, []() {
    if (!guard()) return;
    String name, msg;
    const bool ok = pathRecStop(server.arg("name").c_str(), &name, &msg);
    reply(ok, msg);
  });
  server.on("/api/paths", HTTP_GET, []() {
    if (!guard()) return;
    sendJson(200, pathListJson());
  });
  server.on("/api/path/load", HTTP_POST, []() {
    if (!guard()) return;
    if (!isIdle(stateSnapshot()) && stateSnapshot().mode == Mode::AUTO) return reply(false, "stop auto mode first");
    String msg;
    const bool ok = pathAutoPrepare(server.arg("name").c_str(), &msg);
    reply(ok, msg);
  });
  server.on("/api/auto/start", HTTP_POST, []() {
    if (!guard()) return;
    const char* why = "";
    if (pathAutoBegin(&why)) reply(true, "auto mode started");
    else reply(false, why);
  });

  // ---- Maintenance (only while disarmed) ------------------------------------------------------
  server.on("/api/ota/check", HTTP_POST, []() {
    if (!guard()) return;
    if (!isIdle(stateSnapshot())) return reply(false, "disarm first: updates only run while the rover is stopped");
    String msg;
    otaCheckNow(&msg, false);  // look only
    reply(true, msg);
  });
  server.on("/api/ota/install", HTTP_POST, []() {
    if (!guard()) return;
    if (!isIdle(stateSnapshot())) return reply(false, "disarm first: updates only run while the rover is stopped");
    String msg;
    otaCheckNow(&msg, false);  // is there anything newer?
    if (!otaStatus().available) return reply(true, msg);
    reply(true, "installing: the rover restarts by itself when it is done (about a minute)");  // answer BEFORE flashing
    otaCheckNow(&msg, true);   // reboots into the new firmware on success
  });
  server.on("/api/config/sync", HTTP_POST, []() {
    if (!guard()) return;
    if (!isIdle(stateSnapshot())) return reply(false, "disarm first");
    String msg;
    const bool applied = runtimeConfigSync(&msg);
    reply(true, msg + (applied ? "" : ""));
  });
  server.on("/api/imu/calibrate", HTTP_POST, []() {
    if (!guard()) return;
    if (!isIdle(stateSnapshot())) return reply(false, "disarm first");
    imuRequestCalibration();
    reply(true, "calibrating for ~3 s: keep the rover still");
  });
  server.on("/api/bench", HTTP_POST, []() {  // wiring check: spin one wheel for a second
    if (!guard()) return;
    if (!isIdle(stateSnapshot())) return reply(false, "only while disarmed");
    const int w = server.arg("w").toInt();
    float v = server.arg("v").toFloat();
    if (w < 0 || w >= W_COUNT || !isfinite(v)) return reply(false, "bad wheel or speed");
    v = constrain(v, -0.5f, 0.5f);
    {
      StateLock l;
      g_state.benchWheel = (int8_t)w;
      g_state.benchValue = v;
      g_state.benchUntilMs = millis() + 1000;
    }
    reply(true, "spinning wheel " + String(w) + " for 1 s (wheels off the ground!)");
  });

  wifiRegisterRoutes(server);

  server.onNotFound([]() {
    if (wifiState() == NetState::PORTAL) {  // captive portal: send unknown URLs to the setup page
      server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/setup", true);
      server.send(302, "text/plain", "");
      return;
    }
    server.send(404, "text/plain", "not found");
  });
}

}  // namespace

bool remoteAuthorized(WebServer& s) {
  // On the rover's own (WPA2-protected) network the client is already authenticated.
  if (s.client().localIP() == WiFi.softAPIP() && WiFi.softAPIP() != IPAddress(0, 0, 0, 0)) return true;
  if (millis() < g_lockUntilMs) return false;
  if (constEq(s.arg("k"), accessCode())) {
    g_authFails = 0;
    return true;
  }
  if (++g_authFails >= 5) {  // 5 wrong codes: lock this out for a minute
    g_lockUntilMs = millis() + 60000;
    g_authFails = 0;
  }
  return false;
}

void remoteDenied(WebServer& s) {
  if (millis() < g_lockUntilMs) {
    s.send(429, "application/json", "{\"ok\":false,\"msg\":\"too many wrong codes, wait a minute\"}");
    return;
  }
  s.send(401, "application/json", "{\"ok\":false,\"msg\":\"access code needed\"}");
}

void remoteBegin() {
  registerRoutes();
  server.begin();
  Serial.println("[web] control page on port 80");
}

void remoteUpdate() { server.handleClient(); }
#endif
