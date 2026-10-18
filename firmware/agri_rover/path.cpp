#include "path.h"

#include <math.h>
#include <vector>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthR = 6371008.8;  // mean radius, metres
inline double rad(double d) { return d * kPi / 180.0; }
inline double deg(double r) { return r * 180.0 / kPi; }
}  // namespace

double wrap180(double d) {
  if (!isfinite(d)) return 0.0;
  d = fmod(d, 360.0);
  if (d > 180.0) d -= 360.0;
  if (d <= -180.0) d += 360.0;
  return d;
}

double wrap360(double d) {
  if (!isfinite(d)) return 0.0;
  d = fmod(d, 360.0);
  if (d < 0) d += 360.0;
  return d;
}

double haversineM(const LatLon& a, const LatLon& b) {
  const double p1 = rad(a.lat), p2 = rad(b.lat);
  const double dp = p2 - p1, dl = rad(b.lon - a.lon);
  const double h = sin(dp / 2) * sin(dp / 2) + cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2);
  return 2.0 * kEarthR * asin(fmin(1.0, sqrt(h)));
}

double bearingDeg(const LatLon& a, const LatLon& b) {
  const double p1 = rad(a.lat), p2 = rad(b.lat), dl = rad(b.lon - a.lon);
  const double y = sin(dl) * cos(p2);
  const double x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl);
  return wrap360(deg(atan2(y, x)));
}

void Projector::init(const LatLon& origin) {
  o_ = origin;
  mPerDegLat_ = kEarthR * kPi / 180.0;
  mPerDegLon_ = mPerDegLat_ * cos(rad(origin.lat));
}

XY Projector::toXY(const LatLon& p) const {
  XY r;
  r.x = (p.lon - o_.lon) * mPerDegLon_;
  r.y = (p.lat - o_.lat) * mPerDegLat_;
  return r;
}

LatLon Projector::toLatLon(const XY& p) const {
  LatLon r;
  r.lat = o_.lat + p.y / mPerDegLat_;
  r.lon = o_.lon + (mPerDegLon_ > 1e-9 ? p.x / mPerDegLon_ : 0.0);
  return r;
}

double distXY(const XY& a, const XY& b) { return hypot(a.x - b.x, a.y - b.y); }

double distPointSegment(const XY& p, const XY& a, const XY& b) {
  const double dx = b.x - a.x, dy = b.y - a.y;
  const double len2 = dx * dx + dy * dy;
  if (len2 < 1e-12) return distXY(p, a);
  double t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2;
  t = fmax(0.0, fmin(1.0, t));
  return distXY(p, XY{a.x + t * dx, a.y + t * dy});
}

bool latLonPlausible(double lat, double lon) {
  return isfinite(lat) && isfinite(lon) && lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0 &&
         !(lat == 0.0 && lon == 0.0);  // (0,0) is what an uninitialised receiver reports
}

size_t simplifyPath(const LatLon* in, size_t n, double epsM, LatLon* out, size_t cap) {
  if (!in || !out || cap == 0 || n == 0) return 0;
  if (n <= 2) {
    size_t c = 0;
    for (size_t i = 0; i < n && c < cap; i++) out[c++] = in[i];
    return c;
  }
  Projector pr;
  pr.init(in[0]);
  std::vector<XY> xy(n);
  for (size_t i = 0; i < n; i++) xy[i] = pr.toXY(in[i]);

  std::vector<unsigned char> keep(n, 0);
  keep[0] = keep[n - 1] = 1;
  std::vector<std::pair<size_t, size_t>> stack;
  stack.push_back({0, n - 1});
  while (!stack.empty()) {
    auto [lo, hi] = stack.back();
    stack.pop_back();
    if (hi <= lo + 1) continue;
    double best = -1.0;
    size_t bestI = lo;
    for (size_t i = lo + 1; i < hi; i++) {
      const double d = distPointSegment(xy[i], xy[lo], xy[hi]);
      if (d > best) {
        best = d;
        bestI = i;
      }
    }
    if (best > epsM) {
      keep[bestI] = 1;
      stack.push_back({lo, bestI});
      stack.push_back({bestI, hi});
    }
  }
  size_t c = 0;
  for (size_t i = 0; i < n && c < cap; i++) {
    if (keep[i]) out[c++] = in[i];
  }
  return c;
}

namespace {
inline double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

double median3(double a, double b, double c) {
  if (a > b) { double t = a; a = b; b = t; }
  if (b > c) { double t = b; b = c; c = t; }
  if (a > b) { double t = a; a = b; b = t; }
  return b;
}
}  // namespace

const char* followStateName(FollowState s) {
  switch (s) {
    case FollowState::IDLE: return "IDLE";
    case FollowState::RUNNING: return "RUNNING";
    case FollowState::FINISHED: return "FINISHED";
    case FollowState::FAULT_OFFPATH: return "OFF_PATH";
    case FollowState::FAULT_NOFIX: return "NO_FIX";
    case FollowState::FAULT_NOHEADING: return "NO_HEADING";
    case FollowState::FAULT_STUCK: return "STUCK";
    case FollowState::FAULT_BADPATH: return "BAD_PATH";
  }
  return "?";
}

bool PathFollower::load(const LatLon* pts, size_t n, const FollowParams& p, const char** err) {
  const char* dummy;
  if (!err) err = &dummy;
  state_ = FollowState::IDLE;
  n_ = 0;
  if (!pts || n < 2) {
    *err = "path needs at least 2 points";
    return false;
  }
  if (n > (size_t)MAX_PATH_POINTS) {
    *err = "path has too many points";
    return false;
  }
  for (size_t i = 0; i < n; i++) {
    if (!latLonPlausible(pts[i].lat, pts[i].lon)) {
      *err = "path has an invalid coordinate";
      return false;
    }
  }
  p_ = p;
  p_.acceptRadiusM = clampd(p_.acceptRadiusM, 1.0, 20.0);
  p_.offPathLimitM = fmax(p_.offPathLimitM, p_.acceptRadiusM + 2.0);  // else it would trip while cutting corners
  if (p_.offPathFixes < 1) p_.offPathFixes = 1;
  proj_.init(pts[0]);

  size_t m = 0;
  for (size_t i = 0; i < n; i++) {
    XY q = proj_.toXY(pts[i]);
    if (m > 0 && distXY(q, pts_[m - 1]) < 0.05) continue;  // drop exact duplicates
    if (m > 0 && distXY(q, pts_[m - 1]) > 500.0) {
      *err = "path has a jump longer than 500 m";
      return false;
    }
    pts_[m++] = q;
  }
  if (m < 2) {
    *err = "path has no length";
    return false;
  }
  totalLen_ = 0;
  for (size_t i = 1; i < m; i++) totalLen_ += distXY(pts_[i - 1], pts_[i]);
  if (totalLen_ < 2.0) {
    *err = "path is shorter than 2 m";
    return false;
  }
  n_ = m;
  idx_ = 1;
  return true;
}

void PathFollower::pushFix(const XY& p) {
  if (nFixes_ < 3) {
    fixes_[nFixes_++] = p;
  } else {
    fixes_[0] = fixes_[1];
    fixes_[1] = fixes_[2];
    fixes_[2] = p;
  }
  if (nFixes_ == 1) {
    filt_ = fixes_[0];
  } else if (nFixes_ == 2) {
    filt_ = XY{(fixes_[0].x + fixes_[1].x) / 2, (fixes_[0].y + fixes_[1].y) / 2};
  } else {
    filt_ = XY{median3(fixes_[0].x, fixes_[1].x, fixes_[2].x), median3(fixes_[0].y, fixes_[1].y, fixes_[2].y)};
  }
  haveFilt_ = true;
}

void PathFollower::start(const LatLon& pos) {
  if (n_ < 2) {
    state_ = FollowState::FAULT_BADPATH;
    return;
  }
  const XY p = proj_.toXY(pos);
  nFixes_ = 0;
  haveFilt_ = false;
  pushFix(p);
  // Nearest segment: resume from wherever on the path the rover currently is.
  double best = 1e18;
  size_t bestSeg = 0;
  for (size_t i = 0; i + 1 < n_; i++) {
    const double d = distPointSegment(p, pts_[i], pts_[i + 1]);
    if (d < best) {
      best = d;
      bestSeg = i;
    }
  }
  invalidS_ = 0;
  noProgressS_ = 0;
  bestDist_ = 1e18;
  badFixes_ = 0;
  idx_ = bestSeg + 1;
  lastXte_ = best;
  lastDist_ = distXY(p, pts_[idx_]);
  lastErr_ = 0;
  state_ = best > p_.offPathLimitM ? FollowState::FAULT_OFFPATH : FollowState::RUNNING;
}

void PathFollower::stop() { state_ = FollowState::IDLE; }

FollowCmd PathFollower::step(const LatLon& pos, bool newFix, bool posValid, double headingDeg, bool headingValid,
                             double yawRateDps, double dt) {
  FollowCmd cmd;
  if (state_ != FollowState::RUNNING) return cmd;
  if (!(dt > 0) || dt > 1.0) dt = 0.05;

  if (!posValid) {  // brief gaps are normal at 5 Hz; hold still, and give up if it persists
    invalidS_ += dt;
    if (invalidS_ > p_.noFixGraceS) fault(FollowState::FAULT_NOFIX);
    return cmd;
  }
  invalidS_ = 0;
  if (!headingValid || !isfinite(headingDeg)) {
    fault(FollowState::FAULT_NOHEADING);
    return cmd;
  }

  const size_t last = n_ - 1;

  // ---- Position-dependent logic runs once per NEW GPS fix. ----
  if (newFix) {
    pushFix(proj_.toXY(pos));
    // Consume AT MOST ONE waypoint per fix: one bad sample must not skip several.
    if (idx_ < last && distXY(filt_, pts_[idx_]) < p_.acceptRadiusM) {
      idx_++;
      bestDist_ = 1e18;
      noProgressS_ = 0;
    }
    if (idx_ == last && distXY(filt_, pts_[last]) < p_.acceptRadiusM) {
      state_ = FollowState::FINISHED;
      return cmd;
    }

    // Cross-track error = distance to the part of the path we are EXPECTED to be on: a window of
    // segments around the target. (Not just the current segment: right after a waypoint is consumed
    // that segment can be tiny. Not the whole path either: a rover on the wrong row of a field must
    // be reported as off-path.)
    const size_t jLo = idx_ >= 6 ? idx_ - 6 : 0;
    const size_t jHi = (idx_ + 4 < last) ? idx_ + 4 : last;  // segment j spans pts_[j]..pts_[j+1]
    double xte = 1e18;
    for (size_t j = jLo; j < jHi; j++) {
      const double d = distPointSegment(filt_, pts_[j], pts_[j + 1]);
      if (d < xte) xte = d;
    }
    lastXte_ = xte;
    badFixes_ = xte > p_.offPathLimitM ? badFixes_ + 1 : 0;
    if (badFixes_ >= p_.offPathFixes) {
      fault(FollowState::FAULT_OFFPATH);
      return cmd;
    }

    // Progress watchdog (per fix): a jammed wheel or a going-in-circles bug must not run forever.
    const double dWp = distXY(filt_, pts_[idx_]);
    lastDist_ = dWp;
    if (dWp < bestDist_ - p_.stuckProgressM) {
      bestDist_ = dWp;
      noProgressS_ = 0;
    }
  }
  if (!haveFilt_) return cmd;  // nothing to steer by yet

  noProgressS_ += dt;  // reset above whenever progress is made
  if (noProgressS_ > p_.stuckTimeoutS) {
    fault(FollowState::FAULT_STUCK);
    return cmd;
  }

  // ---- Steering runs every tick: filtered position, fresh heading and gyro rate. ----
  const double dx = pts_[idx_].x - filt_.x, dy = pts_[idx_].y - filt_.y;
  const double bearing = atan2(dx, dy) * 180.0 / kPi;  // compass degrees, clockwise from north
  const double err = wrap180(bearing - headingDeg);    // + = target is to the right
  lastErr_ = err;

  const double desiredRate = clampd(p_.headingGain * err, -p_.maxYawRateDps, p_.maxYawRateDps);
  // Feed-forward from the desired rate, plus a correction from the measured gyro rate.
  const double turn = (desiredRate + p_.rateGain * (desiredRate - yawRateDps)) / p_.turnFullDps;
  cmd.turn = (float)clampd(turn, -1.0, 1.0);

  const double aerr = fabs(err);
  if (aerr > p_.turnInPlaceDeg) {
    cmd.throttle = 0.0f;  // pivot first, then drive
  } else {
    const double scale = 1.0 - (1.0 - p_.minThrottleFrac) * (aerr / p_.turnInPlaceDeg);
    cmd.throttle = (float)clampd(p_.autoSpeed * scale, 0.0, 1.0);
  }
  return cmd;
}

FollowStatus PathFollower::status() const {
  FollowStatus s;
  s.state = state_;
  s.wpIndex = (int)idx_;
  s.wpCount = (int)n_;
  s.xteM = lastXte_;
  s.distToWpM = lastDist_;
  s.headingErrDeg = lastErr_;
  s.progress = n_ > 1 ? (double)(idx_ - 1) / (double)(n_ - 1) : 0.0;
  return s;
}

#ifdef ARDUINO
#include <LittleFS.h>
#include <freertos/semphr.h>
#include <time.h>
#include "ota_github.h"
#include "safety.h"
#include "wifi_manager.h"

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
    snprintf(b, sizeof(b), "path_b%lu_%lu", (unsigned long)millis(), (unsigned long)(millis() / 1000));
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
#endif
