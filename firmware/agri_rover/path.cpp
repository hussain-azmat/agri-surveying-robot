#include "path.h"

#include <LittleFS.h>
#include <freertos/semphr.h>
#include <math.h>
#include <time.h>

#include "geo.h"
#include "github_api.h"
#include "settings.h"

namespace {

SemaphoreHandle_t g_pmux = nullptr;  // guards the follower and the recording buffer
PathFollower* g_follower = nullptr;
char g_loadedName[40] = "";

LatLon* g_rec = nullptr;
volatile int g_recN = 0;
volatile bool g_recActive = false;
LatLon g_lastKept;
double g_lastKeptHeading = 0;
bool g_haveHeading = false;

uint32_t g_upBackoffMs = 0, g_upNextMs = 0;

bool lockWait() { return g_pmux && xSemaphoreTake(g_pmux, pdMS_TO_TICKS(200)) == pdTRUE; }
bool lockTry() { return g_pmux && xSemaphoreTake(g_pmux, 0) == pdTRUE; }
void unlock() {
  if (g_pmux) xSemaphoreGive(g_pmux);
}

String fsPathFor(const char* name) { return String("/paths/") + name + ".csv"; }

FollowParams paramsFromConfig(const RuntimeConfig& c) {
  FollowParams p;
  p.acceptRadiusM = c.acceptRadiusM;
  p.offPathLimitM = c.offPathM;
  p.autoSpeed = c.autoSpeed;
  p.turnFullDps = c.turnFullDps;
  return p;
}

// Parses "lat,lon[,...]" lines. A non-numeric first line is treated as a header.
bool readPathFile(const String& fsPath, LatLon* out, int cap, int* n, String* err) {
  File f = LittleFS.open(fsPath, "r");
  if (!f) {
    *err = "path file not found";
    return false;
  }
  *n = 0;
  int lineNo = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    lineNo++;
    if (line.length() == 0 || line[0] == '#') continue;
    const int comma = line.indexOf(',');
    char* e1 = nullptr;
    char* e2 = nullptr;
    const double lat = comma > 0 ? strtod(line.c_str(), &e1) : 0;
    const double lon = comma > 0 ? strtod(line.c_str() + comma + 1, &e2) : 0;
    const bool numeric = comma > 0 && e1 == line.c_str() + comma && e2 != line.c_str() + comma + 1;
    if (!numeric) {
      if (lineNo == 1) continue;  // header
      *err = String("bad line ") + lineNo;
      f.close();
      return false;
    }
    if (!latLonPlausible(lat, lon)) {
      *err = String("implausible coordinate on line ") + lineNo;
      f.close();
      return false;
    }
    if (*n >= cap) {
      *err = "path has too many points";
      f.close();
      return false;
    }
    out[(*n)++] = LatLon{lat, lon};
  }
  f.close();
  return true;
}

String uniqueName(String base) {
  if (!LittleFS.exists(fsPathFor(base.c_str()))) return base;
  for (int i = 2; i < 100; i++) {
    String cand = base + "_" + i;
    if (!LittleFS.exists(fsPathFor(cand.c_str()))) return cand;
  }
  return base + "_" + millis();
}

String defaultName() {
  const time_t now = time(nullptr);
  char b[40];
  if (now > 1704067200) {
    struct tm t;
    gmtime_r(&now, &t);
    strftime(b, sizeof(b), "path_%Y%m%d_%H%M%S", &t);
  } else {
    snprintf(b, sizeof(b), "path_b%lu_%lu", (unsigned long)nvsGetU32("boots", 0), (unsigned long)(millis() / 1000));
  }
  return String(b);
}

}  // namespace

bool pathNameValid(const char* name) {
  if (!name || !*name || strlen(name) > 32) return false;
  for (const char* c = name; *c; c++) {
    if (!(isalnum((unsigned char)*c) || *c == '_' || *c == '-')) return false;
  }
  return true;
}

void pathBegin() {
  if (!g_pmux) g_pmux = xSemaphoreCreateMutex();
  LittleFS.mkdir("/paths");
}

const char* pathLoadedName() { return g_loadedName; }
int pathRecCount() { return g_recN; }

// ---------------------------------------------------------------------- control-task side

bool pathAutoStep(const GpsData& g, bool newFix, const ImuData& im, float dt, FollowCmd* cmd, FollowStatus* st) {
  if (!g_follower || !lockTry()) return false;
  *cmd = g_follower->step(LatLon{g.lat, g.lon}, newFix, g.fix, im.yawDeg, im.ok && im.yawValid, im.yawRateDps, dt);
  *st = g_follower->status();
  unlock();
  return true;
}

void pathRecFeed(const GpsData& g, const ImuData& im, const RuntimeConfig& cfg) {
  if (!g_recActive || !g.fix || g.hdop > cfg.hdopMax) return;
  if (!lockTry()) return;
  if (g_rec && g_recN < MAX_PATH_POINTS) {
    const LatLon p{g.lat, g.lon};
    double heading = 0;
    bool haveHeading = false;
    if (im.ok && im.yawValid) {
      heading = im.yawDeg;
      haveHeading = true;
    } else if (g.courseValid) {
      heading = g.courseDeg;
      haveHeading = true;
    }
    bool keep = false;
    if (g_recN == 0) {
      keep = true;  // always capture where the run started, even before the rover moves
    } else if (g.speedMps >= 0.2f) {
      const double d = haversineM(g_lastKept, p);
      const double turn = (haveHeading && g_haveHeading) ? fabs(wrap180(heading - g_lastKeptHeading)) : 0.0;
      keep = d >= cfg.pathSpacingM || (turn >= cfg.pathTurnDeg && d >= 0.5);
    }
    if (keep) {
      g_rec[g_recN] = p;
      g_recN = g_recN + 1;
      g_lastKept = p;
      g_lastKeptHeading = heading;
      g_haveHeading = haveHeading;
    }
  }
  unlock();
}

// ------------------------------------------------------------------------- main-loop side

bool pathRecStart(const char** why) {
  const char* dummy;
  if (!why) why = &dummy;
  RoverState s = stateSnapshot();
  if (s.mode != Mode::MANUAL) {
    *why = "arm the rover first (manual mode)";
    return false;
  }
  if (s.recording) {
    *why = "already recording";
    return false;
  }
  if (!lockWait()) {
    *why = "busy, try again";
    return false;
  }
  if (g_rec) free(g_rec);
  g_rec = (LatLon*)malloc(sizeof(LatLon) * MAX_PATH_POINTS);
  if (!g_rec) {
    unlock();
    *why = "out of memory";
    return false;
  }
  g_recN = 0;
  g_haveHeading = false;
  g_recActive = true;
  unlock();
  {
    StateLock l;
    g_state.recording = true;
    g_state.recPoints = 0;
  }
  return true;
}

bool pathRecStop(const char* requestedName, String* savedName, String* message) {
  String dummy;
  if (!message) message = &dummy;
  if (!g_recActive) {
    *message = "not recording";
    return false;
  }
  if (!lockWait()) {
    *message = "busy, try again";
    return false;
  }
  g_recActive = false;
  LatLon* pts = g_rec;
  const int n = g_recN;
  g_rec = nullptr;
  unlock();
  {
    StateLock l;
    g_state.recording = false;
  }
  if (!pts) {
    *message = "no recording buffer";
    return false;
  }
  if (n < 2) {
    free(pts);
    *message = "path discarded: fewer than 2 points were recorded (was there a GPS fix and did the rover move?)";
    return false;
  }
  LatLon* thin = (LatLon*)malloc(sizeof(LatLon) * n);
  if (!thin) {
    free(pts);
    *message = "out of memory while saving";
    return false;
  }
  const size_t kept = simplifyPath(pts, n, PATH_SIMPLIFY_EPS_M, thin, n);
  free(pts);

  String name = pathNameValid(requestedName) ? String(requestedName) : defaultName();
  name = uniqueName(name);
  LittleFS.mkdir("/paths");
  File f = LittleFS.open(fsPathFor(name.c_str()), "w");
  if (!f) {
    free(thin);
    *message = "cannot write the path file (flash full?)";
    return false;
  }
  f.println("lat,lon");
  char line[48];
  for (size_t i = 0; i < kept; i++) {
    snprintf(line, sizeof(line), "%.7f,%.7f", thin[i].lat, thin[i].lon);
    f.println(line);
  }
  f.close();
  free(thin);
  File marker = LittleFS.open(fsPathFor(name.c_str()) + ".up", "w");  // "upload me" flag, survives reboots
  if (marker) marker.close();
  if (savedName) *savedName = name;
  *message = String("saved ") + name + " (" + n + " points recorded, " + (int)kept + " kept)";
  return true;
}

bool pathAutoPrepare(const char* name, String* message) {
  String dummy;
  if (!message) message = &dummy;
  if (!pathNameValid(name)) {
    *message = "invalid path name";
    return false;
  }
  const String fp = fsPathFor(name);
  if (!LittleFS.exists(fp)) {
    const String repoPath = String(GH_PATHS_DIR) + "/" + name + ".csv";
    const int rc = ghGetToFile(repoPath.c_str(), fp.c_str(), 64 * 1024);
    if (rc != 200) {
      *message = String("path not on the rover and could not be downloaded: ") + ghLastError();
      return false;
    }
  }
  LatLon* pts = (LatLon*)malloc(sizeof(LatLon) * MAX_PATH_POINTS);
  if (!pts) {
    *message = "out of memory";
    return false;
  }
  int n = 0;
  String err;
  if (!readPathFile(fp, pts, MAX_PATH_POINTS, &n, &err)) {
    free(pts);
    *message = "bad path file: " + err;
    return false;
  }
  const RuntimeConfig cfg = stateSnapshot().cfg;
  if (!lockWait()) {
    free(pts);
    *message = "busy, try again";
    return false;
  }
  if (!g_follower) g_follower = new (std::nothrow) PathFollower();
  bool ok = false;
  if (g_follower) {
    const char* lerr = nullptr;
    ok = g_follower->load(pts, n, paramsFromConfig(cfg), &lerr);
    if (!ok) *message = String("path rejected: ") + (lerr ? lerr : "?");
  } else {
    *message = "out of memory";
  }
  unlock();
  free(pts);
  if (ok) {
    strncpy(g_loadedName, name, sizeof(g_loadedName) - 1);
    g_loadedName[sizeof(g_loadedName) - 1] = '\0';
    *message = String("loaded ") + name + " (" + n + " points)";
  }
  return ok;
}

bool pathAutoBegin(const char** why) {
  const char* dummy;
  if (!why) why = &dummy;
  if (!g_follower || !g_follower->loaded()) {
    *why = "no path loaded";
    return false;
  }
  RoverState s = stateSnapshot();
  if (!s.gps.fix) {
    *why = "no GPS fix";
    return false;
  }
  if (!lockWait()) {
    *why = "busy, try again";
    return false;
  }
  g_follower->start(LatLon{s.gps.lat, s.gps.lon});
  const FollowState fs = g_follower->state();
  unlock();
  if (fs != FollowState::RUNNING) {
    *why = fs == FollowState::FAULT_OFFPATH ? "rover is too far from the path: drive it to the start of the path first"
                                            : followStateName(fs);
    return false;
  }
  if (!stateStartAuto(why)) {
    pathAutoStop();
    return false;
  }
  return true;
}

void pathAutoStop() {
  if (!g_follower) return;
  if (lockWait()) {
    g_follower->stop();
    unlock();
  }
}

String pathListJson() {
  String out = "[";
  File dir = LittleFS.open("/paths");
  bool first = true;
  if (dir && dir.isDirectory()) {
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
      String n = f.name();
      if (!n.endsWith(".csv")) continue;
      const size_t sz = f.size();
      n = n.substring(0, n.length() - 4);
      if (!first) out += ",";
      first = false;
      out += "{\"name\":\"" + n + "\",\"bytes\":" + (unsigned)sz + ",\"pending\":" +
             (LittleFS.exists(fsPathFor(n.c_str()) + ".up") ? "true" : "false") + "}";
    }
  }
  out += "]";
  return out;
}

void pathService(bool online, bool idle) {
  if (!online || !idle || millis() < g_upNextMs) return;
  if (!ghTokenPresent()) return;
  File dir = LittleFS.open("/paths");
  if (!dir || !dir.isDirectory()) return;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String n = f.name();
    if (!n.endsWith(".csv.up")) continue;
    n = n.substring(0, n.length() - 7);
    if (!pathNameValid(n.c_str())) continue;
    const String fp = fsPathFor(n.c_str());
    File data = LittleFS.open(fp, "r");
    if (!data || data.size() == 0 || data.size() > 60 * 1024) {
      if (data) data.close();
      LittleFS.remove(fp + ".up");  // nothing sensible to upload
      return;
    }
    const size_t len = data.size();
    uint8_t* buf = (uint8_t*)malloc(len);
    if (!buf) {
      data.close();
      return;
    }
    data.read(buf, len);
    data.close();
    const String repoPath = String(GH_PATHS_DIR) + "/" + n + ".csv";
    const int rc = ghPutFile(repoPath.c_str(), buf, len, (String("path ") + n + " from " + roverId()).c_str(), true);
    free(buf);
    if (rc == 200 || rc == 201) {
      LittleFS.remove(fp + ".up");
      g_upBackoffMs = 0;
      Serial.printf("[path] uploaded %s\n", n.c_str());
    } else {
      g_upBackoffMs = g_upBackoffMs ? min<uint32_t>(g_upBackoffMs * 2, 600000) : 15000;
      if (ghRetryAfterS()) g_upBackoffMs = max<uint32_t>(g_upBackoffMs, ghRetryAfterS() * 1000UL);
      g_upNextMs = millis() + g_upBackoffMs;
      Serial.printf("[path] upload of %s failed (%d): %s; retry in %lu s\n", n.c_str(), rc, ghLastError(),
                    (unsigned long)(g_upBackoffMs / 1000));
    }
    return;  // one upload per call keeps the main loop responsive
  }
}
