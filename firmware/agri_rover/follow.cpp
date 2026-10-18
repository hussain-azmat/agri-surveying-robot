// PURE: no Arduino dependencies.
#include "follow.h"

#include <math.h>

namespace {
constexpr double kPi = 3.14159265358979323846;
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
