#include "wifi_manager.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <time.h>

#include "config.h"
#include "remote.h"
#include "settings.h"
#include "ui.h"

namespace {

NetState g_state = NetState::BOOT;
uint32_t g_stateSinceMs = 0;
DNSServer g_dns;
bool g_dnsOn = false;
bool g_apOn = false;
bool g_mdnsOn = false;
bool g_ntpOn = false;
uint32_t g_portalStartMs = 0;
uint32_t g_apCloseAtMs = 0;
bool g_scanStarted = false;

// "Test then save" state for the setup page
enum class Test : uint8_t { NONE, RUNNING, OK, FAIL };
Test g_test = Test::NONE;
String g_testSsid, g_testPass, g_testToken, g_testCode, g_testMsg;
uint32_t g_testStartMs = 0;
String g_oldSsid, g_oldPass;

void setState(NetState s) {
  g_state = s;
  g_stateSinceMs = millis();
}

String jsonEscape(const String& s) {
  String o;
  for (size_t i = 0; i < s.length(); i++) {
    const char c = s[i];
    if (c == '"' || c == '\\') {
      o += '\\';
      o += c;
    } else if ((unsigned char)c < 0x20) {
      o += ' ';
    } else {
      o += c;
    }
  }
  return o;
}

void startAp(bool captive) {
  if (!g_apOn) {
    WiFi.mode(WIFI_AP_STA);
    // The password protects the rover's own network; see settings.h for how the access code works.
    WiFi.softAP(apSsid().c_str(), accessCode().c_str());
    g_apOn = true;
  }
  if (captive && !g_dnsOn) {
    g_dns.start(53, "*", WiFi.softAPIP());  // answers every name with the rover: a phone then pops up the setup page
    g_dnsOn = true;
  }
  if (!captive && g_dnsOn) {
    g_dns.stop();
    g_dnsOn = false;
  }
}

void stopAp() {
  if (g_dnsOn) {
    g_dns.stop();
    g_dnsOn = false;
  }
  if (g_apOn) {
    WiFi.softAPdisconnect(true);
    g_apOn = false;
    WiFi.mode(WIFI_STA);
  }
}

void startConnecting() {
  WiFi.mode(g_apOn ? WIFI_AP_STA : WIFI_STA);
  WiFi.begin(wifiSsid().c_str(), wifiPass().c_str());
  setState(NetState::CONNECTING);
}

void startPortal() {
  startAp(true);
  g_portalStartMs = millis();
  g_scanStarted = false;
  g_test = Test::NONE;
  setState(NetState::PORTAL);
  Serial.println("[wifi] setup mode: join the rover's own network and open any web page");
}

void goOffline() {
  startAp(false);  // keep the rover's network up for phone driving, drop the captive DNS
  setState(NetState::OFFLINE_AP);
  Serial.println("[wifi] offline mode: driving works, OTA and uploads are off");
}

void onOnline() {
  if (!g_ntpOn) {
    configTime(0, 0, "pool.ntp.org", "time.google.com");  // UTC; certificate checks need a real clock
    g_ntpOn = true;
  }
  if (!g_mdnsOn && MDNS.begin(roverId().c_str())) {
    MDNS.addService("http", "tcp", 80);
    g_mdnsOn = true;
  }
  setState(NetState::ONLINE);
  Serial.println("[wifi] online");
}

void startTest(const String& ssid, const String& pass) {
  g_oldSsid = wifiSsid();
  g_oldPass = wifiPass();
  g_testSsid = ssid;
  g_testPass = pass;
  g_test = Test::RUNNING;
  g_testStartMs = millis();
  g_testMsg = "";
  WiFi.disconnect(false, false);
  WiFi.begin(ssid.c_str(), pass.c_str());
}

void serviceTest() {
  if (g_test != Test::RUNNING) return;
  const wl_status_t st = WiFi.status();
  if (st == WL_CONNECTED) {
    wifiCredsSave(g_testSsid, g_testPass);
    if (g_testToken.length()) ghTokenSave(g_testToken);
    if (g_testCode.length() >= 8) accessCodeSave(g_testCode);
    g_testToken = g_testPass = String();
    g_test = Test::OK;
    g_testMsg = "Connected and saved.";
    g_apCloseAtMs = millis() + 20000;  // leave the rover's network up long enough for the phone to read the result
    onOnline();
    return;
  }
  const bool failed = st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL;
  if (failed || millis() - g_testStartMs > 20000) {
    g_test = Test::FAIL;
    g_testMsg = st == WL_NO_SSID_AVAIL ? "That network was not found." : "Could not connect. Check the password.";
    g_testPass = g_testToken = String();
    WiFi.disconnect(false, false);
    if (g_oldSsid.length()) WiFi.begin(g_oldSsid.c_str(), g_oldPass.c_str());  // go back to what worked
  }
}

String scanJson() {
  const int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_FAILED || (n == -2 && !g_scanStarted)) {
    WiFi.scanNetworks(true, false);
    g_scanStarted = true;
    return "{\"scanning\":true,\"networks\":[]}";
  }
  if (n == WIFI_SCAN_RUNNING) return "{\"scanning\":true,\"networks\":[]}";
  String out = "{\"scanning\":false,\"networks\":[";
  bool first = true;
  String seen;
  for (int i = 0; i < n && i < 40; i++) {
    const String ssid = WiFi.SSID(i);
    if (ssid.length() == 0 || seen.indexOf("\x1f" + ssid + "\x1f") >= 0) continue;
    seen += "\x1f" + ssid + "\x1f";
    if (!first) out += ",";
    first = false;
    out += "{\"ssid\":\"" + jsonEscape(ssid) + "\",\"rssi\":" + WiFi.RSSI(i) + ",\"open\":" +
           (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "true" : "false") + "}";
  }
  out += "]}";
  WiFi.scanDelete();
  g_scanStarted = false;
  return out;
}

void redirectToSetup(WebServer& s) {
  s.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/setup", true);
  s.send(302, "text/plain", "");
}

}  // namespace

bool setupButtonHeldAtBoot() {
  pinMode(PIN_SETUP_BTN, INPUT_PULLUP);
  pinMode(PIN_STATUS_LED, OUTPUT);
  if (digitalRead(PIN_SETUP_BTN) != LOW) return false;
  const uint32_t t0 = millis();
  while (digitalRead(PIN_SETUP_BTN) == LOW) {
    digitalWrite(PIN_STATUS_LED, ((millis() - t0) / 150) % 2);
    if (millis() - t0 >= SETUP_BTN_HOLD_MS) {
      digitalWrite(PIN_STATUS_LED, HIGH);
      Serial.println("[wifi] setup button held: entering WiFi setup");
      return true;
    }
    delay(10);
  }
  digitalWrite(PIN_STATUS_LED, LOW);
  return false;  // released early: a normal boot
}

void wifiBegin(bool forcePortal) {
  WiFi.persistent(false);  // we manage credentials ourselves, in NVS
  WiFi.setAutoReconnect(true);
  WiFi.setHostname(roverId().c_str());
  setState(NetState::BOOT);
  if (forcePortal || !wifiCredsPresent()) startPortal();
  else startConnecting();
}

void wifiUpdate() {
  const uint32_t now = millis();
  if (g_dnsOn) g_dns.processNextRequest();
  serviceTest();

  switch (g_state) {
    case NetState::CONNECTING:
      if (WiFi.status() == WL_CONNECTED) onOnline();
      else if (now - g_stateSinceMs > WIFI_CONNECT_TIMEOUT_MS) startPortal();  // wrong password / network gone
      break;

    case NetState::ONLINE:
      if (g_apCloseAtMs && now > g_apCloseAtMs) {
        g_apCloseAtMs = 0;
        stopAp();
      }
      if (WiFi.status() != WL_CONNECTED && now - g_stateSinceMs > 5000) {
        // Link dropped. Keep driving possible via our own network while auto-reconnect keeps trying.
        goOffline();
      } else if (WiFi.status() == WL_CONNECTED) {
        g_stateSinceMs = now;  // "last seen connected"
      }
      break;

    case NetState::PORTAL:
      if (g_test == Test::NONE || g_test == Test::FAIL) {
        if (WiFi.status() == WL_CONNECTED && wifiCredsPresent()) {  // reached via "Change WiFi" and still connected
          // stay in setup until the user finishes or the timeout below hands back to ONLINE
        }
        if (now - g_portalStartMs > WIFI_PORTAL_TIMEOUT_MS) {
          if (WiFi.status() == WL_CONNECTED) {
            stopAp();
            onOnline();
          } else {
            goOffline();
          }
        }
      }
      break;

    case NetState::OFFLINE_AP:
      // If saved credentials exist, auto-reconnect is already trying; take the network back when it appears.
      if (wifiCredsPresent() && WiFi.status() == WL_CONNECTED) {
        stopAp();
        onOnline();
      }
      break;

    case NetState::BOOT:
      break;
  }

  if (g_state == NetState::ONLINE && g_apCloseAtMs && now > g_apCloseAtMs) {
    g_apCloseAtMs = 0;
    stopAp();
  }
}

NetState wifiState() { return g_state; }
const char* wifiStateName() {
  switch (g_state) {
    case NetState::BOOT: return "BOOT";
    case NetState::CONNECTING: return "CONNECTING";
    case NetState::ONLINE: return "ONLINE";
    case NetState::PORTAL: return "SETUP";
    case NetState::OFFLINE_AP: return "OFFLINE";
  }
  return "?";
}
bool wifiOnline() { return WiFi.status() == WL_CONNECTED; }
bool wifiApActive() { return g_apOn; }
String wifiIp() { return WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString(); }
String wifiHost() { return g_mdnsOn ? roverId() + ".local" : String(""); }
bool wifiTimeSynced() { return time(nullptr) > 1704067200; }
void wifiRequestPortal() { startPortal(); }

void wifiRegisterRoutes(WebServer& server) {
  WebServer* s = &server;

  s->on("/setup", HTTP_GET, [s]() {
    if (!remoteAuthorized(*s)) return remoteDenied(*s);
    if (g_state != NetState::PORTAL) startPortal();  // opening the page puts the rover in setup mode
    s->send_P(200, "text/html", UI_SETUP_HTML);
  });

  s->on("/api/setup/scan", HTTP_GET, [s]() {
    if (!remoteAuthorized(*s)) return remoteDenied(*s);
    s->send(200, "application/json", scanJson());
  });

  s->on("/api/setup/connect", HTTP_POST, [s]() {
    if (!remoteAuthorized(*s)) return remoteDenied(*s);
    const String ssid = s->arg("ssid"), pass = s->arg("pass"), token = s->arg("token"), code = s->arg("code");
    if (ssid.length() < 1 || ssid.length() > 32 || pass.length() > 63 || (pass.length() > 0 && pass.length() < 8)) {
      s->send(400, "application/json", "{\"ok\":false,\"msg\":\"Network name or password length is not valid.\"}");
      return;
    }
    if (token.length() > 255 || (code.length() > 0 && (code.length() < 8 || code.length() > 32))) {
      s->send(400, "application/json", "{\"ok\":false,\"msg\":\"The token or access code length is not valid (access code: 8 to 32 characters).\"}");
      return;
    }
    if (g_test == Test::RUNNING) {
      s->send(409, "application/json", "{\"ok\":false,\"msg\":\"A test is already running.\"}");
      return;
    }
    g_testToken = token;
    g_testCode = code;
    startTest(ssid, pass);
    s->send(200, "application/json", "{\"ok\":true}");
  });

  s->on("/api/setup/status", HTTP_GET, [s]() {
    if (!remoteAuthorized(*s)) return remoteDenied(*s);
    const char* t = g_test == Test::RUNNING ? "running" : g_test == Test::OK ? "ok" : g_test == Test::FAIL ? "fail" : "idle";
    String o = String("{\"test\":\"") + t + "\",\"msg\":\"" + jsonEscape(g_testMsg) + "\",\"net\":\"" + wifiStateName() +
               "\",\"ip\":\"" + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("")) + "\",\"host\":\"" +
               wifiHost() + "\",\"hasToken\":" + (ghTokenPresent() ? "true" : "false") + ",\"id\":\"" + roverId() + "\"}";
    s->send(200, "application/json", o);
  });

  s->on("/api/setup/forget", HTTP_POST, [s]() {
    if (!remoteAuthorized(*s)) return remoteDenied(*s);
    wifiCredsClear();
    s->send(200, "application/json", "{\"ok\":true}");
  });

  // Captive-portal probes: phones/laptops ask these to decide whether to pop up a sign-in page.
  auto probe = [s]() {
    if (g_state == NetState::PORTAL) return redirectToSetup(*s);
    s->send(204, "text/plain", "");
  };
  for (const char* path : {"/generate_204", "/gen_204", "/hotspot-detect.html", "/library/test/success.html",
                           "/connecttest.txt", "/ncsi.txt", "/redirect", "/fwlink", "/canonical.html"}) {
    s->on(path, HTTP_ANY, probe);
  }
}
