#pragma once
// Path: geometry, path following, and path recording/saving/loading.
//   top half  : geo (distances, bearings, projection, thinning) and the PathFollower. Plain C++, unit-tested
//               on a laptop, including closed-loop simulations with GPS noise.
//   under #ifdef ARDUINO: record while driving, save/load CSV in flash, upload to the repo's paths/ folder.
#include <stddef.h>
#include <stdint.h>
#include "config.h"

struct LatLon {
  double lat = 0, lon = 0;
};
struct XY {
  double x = 0, y = 0;  // metres east, metres north of the projector origin
};

double wrap180(double deg);  // -> (-180, 180]
double wrap360(double deg);  // -> [0, 360)

// Great-circle distance (Haversine) in metres.
double haversineM(const LatLon& a, const LatLon& b);
// Initial bearing from a to b, degrees clockwise from true north, [0, 360).
double bearingDeg(const LatLon& a, const LatLon& b);

// Local flat-earth projection around an origin. Error is well under a centimetre per
// kilometre at field scale, and it avoids trigonometry on every GPS update.
class Projector {
 public:
  void init(const LatLon& origin);
  XY toXY(const LatLon& p) const;
  LatLon toLatLon(const XY& p) const;
  const LatLon& origin() const { return o_; }

 private:
  LatLon o_;
  double mPerDegLat_ = 111320.0, mPerDegLon_ = 111320.0;
};

double distXY(const XY& a, const XY& b);
// Distance from p to the segment a-b (clamped to the endpoints).
double distPointSegment(const XY& p, const XY& a, const XY& b);

bool latLonPlausible(double lat, double lon);

// Douglas-Peucker simplification. Keeps the first and last points. Writes at most `cap`
// points to `out` and returns the count. Iterative (no recursion), O(n log n) typical.
size_t simplifyPath(const LatLon* in, size_t n, double epsM, LatLon* out, size_t cap);

//
// Waypoint follower for a skid-steer rover. The rover heads for the next waypoint; a
// waypoint counts as reached inside `acceptRadiusM` (the brief asks for 3-5 m, matching
// the NEO-6M's ~2.5 m accuracy). Heading comes from the caller (gyro-integrated yaw,
// corrected by GPS course), steering is a proportional yaw-rate command with gyro
// feedback, and the follower STOPS (it never guesses) on:
//   - end of path                     -> FINISHED
//   - cross-track error too large     -> FAULT_OFFPATH   (needs several consecutive bad fixes)
//   - no GPS fix for too long         -> FAULT_NOFIX
//   - heading not available           -> FAULT_NOHEADING
//   - no progress towards a waypoint  -> FAULT_STUCK
//
// GPS robustness (found by simulation): the position used for navigation is the median of
// the last three fixes, at most ONE waypoint is consumed per new fix, and the off-path
// check needs consecutive bad fixes. One multipath jump therefore cannot skip waypoints
// or stop the rover.


struct FollowParams {
  double acceptRadiusM = DEF_ACCEPT_RADIUS_M;
  double offPathLimitM = DEF_OFFPATH_M;  // must exceed acceptRadiusM; load() enforces +2 m
  int offPathFixes = 3;                  // consecutive bad fixes before OFF_PATH (3 fixes = 0.6 s at 5 Hz)
  double autoSpeed = DEF_AUTO_SPEED;     // throttle 0..1 on straights
  double turnInPlaceDeg = 50.0;          // heading error above which the rover pivots on the spot
  double minThrottleFrac = 0.3;          // lowest fraction of autoSpeed while turning
  double headingGain = 1.0;              // desired yaw rate (deg/s) per degree of heading error
  double maxYawRateDps = 60.0;
  double turnFullDps = 90.0;             // yaw rate produced by turn = 1. MUST be calibrated on the real rover
  double rateGain = 0.5;                 // gyro feedback gain
  double noFixGraceS = 1.5;
  double stuckTimeoutS = 30.0;           // no progress for this long -> FAULT_STUCK
  double stuckProgressM = 0.5;           // "progress" = the waypoint distance shrank by this much
};

enum class FollowState : uint8_t {
  IDLE,
  RUNNING,
  FINISHED,
  FAULT_OFFPATH,
  FAULT_NOFIX,
  FAULT_NOHEADING,
  FAULT_STUCK,
  FAULT_BADPATH
};

const char* followStateName(FollowState s);

struct FollowCmd {
  float throttle = 0;  // 0..1 (the follower never reverses)
  float turn = 0;      // -1..1, positive = clockwise / right
};

struct FollowStatus {
  FollowState state = FollowState::IDLE;
  int wpIndex = 0;
  int wpCount = 0;
  double xteM = 0;          // cross-track error (of the filtered position)
  double distToWpM = 0;
  double headingErrDeg = 0;
  double progress = 0;      // 0..1 along the polyline (by waypoint index)
};

class PathFollower {
 public:
  // Validates and stores the path. Returns false and sets *err (a static string) if unusable.
  bool load(const LatLon* pts, size_t n, const FollowParams& p, const char** err);
  bool loaded() const { return n_ >= 2; }
  size_t count() const { return n_; }
  // Begins following from the segment nearest to `pos`. If `pos` is farther than the
  // off-path limit from the path, the state becomes FAULT_OFFPATH (move the rover to the path).
  void start(const LatLon& pos);
  void stop();
  // Call every control tick. `newFix` is true only on the tick where `pos` is a NEW GPS sample
  // (5 Hz); position-dependent logic runs on those, steering runs every tick on the filtered
  // position and the fresh heading/yaw-rate.
  FollowCmd step(const LatLon& pos, bool newFix, bool posValid, double headingDeg, bool headingValid,
                 double yawRateDps, double dt);
  FollowStatus status() const;
  FollowState state() const { return state_; }
  double totalLengthM() const { return totalLen_; }

 private:
  void fault(FollowState s) { state_ = s; }
  void pushFix(const XY& p);
  Projector proj_;
  XY pts_[MAX_PATH_POINTS];
  size_t n_ = 0;
  FollowParams p_;
  FollowState state_ = FollowState::IDLE;
  size_t idx_ = 1;  // index of the waypoint being approached
  double invalidS_ = 0;
  double totalLen_ = 0;
  double bestDist_ = 1e18, noProgressS_ = 0;
  double lastXte_ = 0, lastDist_ = 0, lastErr_ = 0;
  XY fixes_[3];
  int nFixes_ = 0;
  XY filt_;
  bool haveFilt_ = false;
  int badFixes_ = 0;
};

#ifdef ARDUINO
#include <Arduino.h>
#include "gps.h"
#include "imu.h"

// Path recording, saving, loading and following (Stages 4 and 5).
//
//  RECORD  while driving manually, a point is kept every ~1.5 m or on a noticeable turn (both
//          tunable in config/rover.cfg). On stop the path is thinned (Douglas-Peucker), saved to
//          flash as paths/<name>.csv and queued for upload to the repo's paths/ folder.
//  FOLLOW  paths/<name>.csv is loaded (downloaded from the repo if not cached), and the pure
//          PathFollower (above) steers the rover along it; see its header comment for the stop rules.
//
// Two kinds of caller, two kinds of function:
//   * control-task side: pathAutoStep / pathRecFeed. Never block, never touch flash or the network.
//   * main-loop side: everything else. May take the path lock, use flash and the network.


void pathBegin();

// ---- control-task side ---------------------------------------------------------------------
// One follower tick. Returns false (and leaves outputs untouched) if the path lock was busy this tick.
bool pathAutoStep(const GpsData& g, bool newFix, const ImuData& im, float dt, FollowCmd* cmd, FollowStatus* st);
// Call on every NEW GPS fix. Cheap; ignored unless recording.
void pathRecFeed(const GpsData& g, const ImuData& im, const RuntimeConfig& cfg);

// ---- main-loop side --------------------------------------------------------------------------
bool pathRecStart(const char** why);
// Stops, thins and saves. `requestedName` may be empty. On success *savedName is the final name.
bool pathRecStop(const char* requestedName, String* savedName, String* message);
int pathRecCount();

// Loads paths/<name>.csv into the follower (downloading it if it is not cached and we are online).
bool pathAutoPrepare(const char* name, String* message);
// Starts following from the nearest point and switches the rover to AUTO. Needs a fix and heading.
bool pathAutoBegin(const char** why);
void pathAutoStop();
const char* pathLoadedName();

// JSON array of {"name","bytes","pending"} for paths saved on the rover.
String pathListJson();
bool pathNameValid(const char* name);

// Uploads queued paths. Call from the main loop; does nothing unless online and idle.
void pathService(bool online, bool idle);
#endif
