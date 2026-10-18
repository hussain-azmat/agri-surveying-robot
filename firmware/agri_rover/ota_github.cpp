#include "ota_github.h"

#include <Update.h>

#include "config.h"
#include "fwversion.h"
#include "github_api.h"
#include "settings.h"
#include "state.h"

namespace {

OtaStatus g_st = {"idle", 0, "", "", 0xFFFFFFFF};
uint32_t g_lastCheckMs = 0;
uint32_t g_nextMs = 0;
uint32_t g_backoffMs = 0;
bool g_firstCheckDone = false;
bool g_busy = false;

void setStatus(const char* state, const char* msg, int pct = 0) {
  strncpy(g_st.state, state, sizeof(g_st.state) - 1);
  g_st.state[sizeof(g_st.state) - 1] = '\0';
  strncpy(g_st.message, msg, sizeof(g_st.message) - 1);
  g_st.message[sizeof(g_st.message) - 1] = '\0';
  g_st.percent = pct;
}

struct FlashCtx {
  size_t written = 0, total = 0;
  int lastPct = -1;
};

bool flashSink(const uint8_t* d, size_t n, void* vctx) {
  FlashCtx* c = (FlashCtx*)vctx;
  if (Update.write((uint8_t*)d, n) != n) return false;  // flash full / write error: abort
  c->written += n;
  const int pct = (int)((c->written * 100ULL) / c->total);
  if (pct != c->lastPct) {
    c->lastPct = pct;
    g_st.percent = pct;
  }
  return true;
}

bool stillSafe() {
  // The flash must never start (or continue) if the rover could be moving.
  return stateSnapshot().mode == Mode::DISARMED;
}

// Returns true if a new image was flashed (the caller then reboots).
bool checkAndUpdate(String* msg) {
  g_busy = true;
  g_lastCheckMs = millis();
  setStatus("checking", "checking for a newer version...");

  struct Done {
    bool* busy;
    ~Done() { *busy = false; }
  } done{&g_busy};

  if (!stillSafe()) {
    *msg = "not checking: the rover is armed";
    setStatus("idle", msg->c_str());
    return false;
  }
  if (!ghReady()) {
    *msg = "offline (or clock not set yet)";
    setStatus("error", msg->c_str());
    return false;
  }

  char buf[320];
  size_t n = 0;
  const int rc = ghGetRaw(GH_OTA_VERSION_PATH, (uint8_t*)buf, sizeof(buf), &n);
  if (rc == 404) {
    *msg = "no ota/version.txt in the repo yet";
    setStatus("idle", msg->c_str());
    return false;
  }
  if (rc != 200) {
    *msg = String("could not read version.txt: ") + ghLastError();
    setStatus("error", msg->c_str());
    return false;
  }

  OtaManifest m;
  const char* perr = nullptr;
  if (!parseOtaManifest(buf, &m, &perr)) {
    *msg = String("version.txt is not usable: ") + perr;
    setStatus("error", msg->c_str());
    return false;
  }
  strncpy(g_st.remoteVersion, m.version, sizeof(g_st.remoteVersion) - 1);

  switch (decideOta(FW_VERSION, m.version)) {
    case OtaDecision::UP_TO_DATE:
      *msg = String("up to date (v") + FW_VERSION + ")";
      setStatus("uptodate", msg->c_str());
      return false;
    case OtaDecision::REMOTE_OLDER:
      *msg = String("repo has v") + m.version + ", older than the running v" + FW_VERSION + ": ignored";
      setStatus("uptodate", msg->c_str());
      return false;
    case OtaDecision::INVALID:
      *msg = "version numbers are not valid x.y.z";
      setStatus("error", msg->c_str());
      return false;
    case OtaDecision::UPDATE:
      break;
  }

  // ---- A newer version exists: download it into the INACTIVE slot, verifying as we go. ----
  Serial.printf("[ota] updating v%s -> v%s (%lu bytes)\n", FW_VERSION, m.version, (unsigned long)m.size);
  setStatus("downloading", "downloading update...", 0);
  if (!Update.begin(m.size, U_FLASH)) {
    *msg = String("cannot start the update: ") + Update.errorString();
    setStatus("error", msg->c_str());
    Update.abort();
    return false;
  }
  Update.setMD5(m.md5);  // end() refuses to boot the image unless this matches

  FlashCtx ctx;
  ctx.total = m.size;
  size_t got = 0;
  const int drc = ghDownload(GH_OTA_FIRMWARE_PATH, flashSink, &ctx, (long)m.size, m.size + 1024, &got);
  if (drc != 200) {
    Update.abort();
    *msg = String("download failed, old firmware kept: ") + ghLastError();
    setStatus("error", msg->c_str());
    return false;
  }
  if (!stillSafe()) {  // someone armed the rover during the download: do not reboot under them
    Update.abort();
    *msg = "update aborted: the rover was armed during the download";
    setStatus("error", msg->c_str());
    return false;
  }
  if (!Update.end(false)) {  // false: the byte count must equal what begin() was told, and the MD5 must match
    *msg = String("image failed verification, old firmware kept: ") + Update.errorString();
    setStatus("error", msg->c_str());
    return false;
  }

  otaMarkUpdated(FW_VERSION);
  *msg = String("updated to v") + m.version + ", restarting";
  setStatus("idle", msg->c_str(), 100);
  Serial.println("[ota] update verified; restarting into the new firmware (it will stay disarmed)");
  return true;
}

}  // namespace

void otaBegin() { g_nextMs = millis() + 15000; }  // first check ~15 s after boot, once WiFi has had time

OtaStatus otaStatus() {
  OtaStatus s = g_st;
  s.lastCheckAgoS = g_lastCheckMs ? (millis() - g_lastCheckMs) / 1000 : 0xFFFFFFFF;
  return s;
}

void otaCheckNow(String* message) {
  String dummy;
  if (!message) message = &dummy;
  if (g_busy) {
    *message = "a check is already running";
    return;
  }
  if (checkAndUpdate(message)) {
    delay(400);
    ESP.restart();
  }
}

void otaService(bool online, bool idle) {
  if (g_busy || !online || !idle) return;
  const uint32_t now = millis();
  if ((int32_t)(now - g_nextMs) < 0) return;
  String msg;
  const bool flashed = checkAndUpdate(&msg);
  Serial.printf("[ota] %s\n", msg.c_str());
  if (flashed) {
    delay(400);
    ESP.restart();
    return;
  }
  g_firstCheckDone = true;
  const uint32_t interval = stateSnapshot().cfg.otaIntervalS * 1000UL;
  if (strcmp(g_st.state, "error") == 0) {
    g_backoffMs = g_backoffMs ? min<uint32_t>(g_backoffMs * 2, interval) : 30000;
    if (ghRetryAfterS()) g_backoffMs = max<uint32_t>(g_backoffMs, ghRetryAfterS() * 1000UL);
    g_nextMs = now + g_backoffMs;
  } else {
    g_backoffMs = 0;
    g_nextMs = now + interval;
  }
}
