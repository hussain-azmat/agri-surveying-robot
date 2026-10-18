#pragma once
// PURE: no Arduino dependencies. Unit-tested on a laptop (test/test_geo_follow.cpp),
// including closed-loop simulations with GPS noise and outliers.
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
#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "geo.h"

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
