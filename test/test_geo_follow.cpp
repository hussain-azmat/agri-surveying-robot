#include <math.h>
#include <stdio.h>

#include <vector>

#include "follow.h"
#include "geo.h"
#include "mini_test.h"

namespace {

const LatLon kOrigin{31.5204, 74.3587};  // Lahore: an arbitrary but realistic test field

// Deterministic Gaussian noise (no <random>, so results are identical on every machine).
struct Rng {
  uint32_t s = 12345;
  double u() {
    s = s * 1664525u + 1013904223u;
    return ((s >> 8) & 0xFFFFFF) / 16777216.0 + 1e-9;
  }
  double gauss() { return sqrt(-2.0 * log(u())) * cos(6.283185307179586 * u()); }
};

std::vector<LatLon> fromXY(const std::vector<XY>& xy) {
  Projector pr;
  pr.init(kOrigin);
  std::vector<LatLon> out;
  for (const XY& p : xy) out.push_back(pr.toLatLon(p));
  return out;
}

// Densify a polyline to ~spacing metres, as the recorder would produce.
std::vector<XY> densify(const std::vector<XY>& poly, double spacing) {
  std::vector<XY> out;
  for (size_t i = 0; i + 1 < poly.size(); i++) {
    const double len = distXY(poly[i], poly[i + 1]);
    const int n = (int)ceil(len / spacing);
    for (int k = 0; k < n; k++) {
      const double t = (double)k / n;
      out.push_back({poly[i].x + t * (poly[i + 1].x - poly[i].x), poly[i].y + t * (poly[i + 1].y - poly[i].y)});
    }
  }
  out.push_back(poly.back());
  return out;
}

double trueXte(const std::vector<XY>& path, const XY& p) {
  double best = 1e18;
  for (size_t i = 0; i + 1 < path.size(); i++) best = fmin(best, distPointSegment(p, path[i], path[i + 1]));
  return best;
}

struct SimOptions {
  double gpsSigma = 0.0;       // Gaussian error per axis, metres
  double outlierProb = 0.0;    // probability that a fix is a multipath jump
  double outlierM = 12.0;      // size of such a jump
  double headingBiasDeg = 0.0; // uncorrected gyro drift
  double maxTimeS = 900.0;
  double startOffsetX = 0.0;
};

struct SimResult {
  FollowState end;
  double maxXte;
  double timeS;
  XY finalPos;
};

// Closed-loop simulation: unicycle plant with a first-order yaw lag; GPS fixes arrive at 5 Hz
// (held between fixes), with Gaussian noise and optional multipath outliers.
SimResult simulate(const std::vector<XY>& path, const SimOptions& o, const FollowParams& fp) {
  PathFollower f;
  const char* err = nullptr;
  auto ll = fromXY(path);
  CHECK(f.load(ll.data(), ll.size(), fp, &err));
  Projector pr;
  pr.init(ll[0]);

  XY pos{path[0].x + o.startOffsetX, path[0].y};
  double heading = 0, omega = 0;  // compass degrees, deg/s clockwise
  Rng rng;
  XY meas = pos;
  f.start(pr.toLatLon(pos));
  SimResult r{f.state(), 0, 0, pos};
  if (f.state() != FollowState::RUNNING) return r;

  const double dt = 0.05;  // 20 Hz control ticks; a GPS fix every 4th tick
  int tick = 0;
  for (double t = 0; t < o.maxTimeS; t += dt, tick++) {
    const bool newFix = (tick % 4) == 0;
    if (newFix) {
      meas = {pos.x + rng.gauss() * o.gpsSigma, pos.y + rng.gauss() * o.gpsSigma};
      if (o.outlierProb > 0 && rng.u() < o.outlierProb) {
        const double a = rng.u() * 6.283185307179586;
        meas.x += o.outlierM * cos(a);
        meas.y += o.outlierM * sin(a);
      }
    }
    FollowCmd c = f.step(pr.toLatLon(meas), newFix, true, heading + o.headingBiasDeg, true, omega, dt);
    if (f.state() != FollowState::RUNNING) {
      r.end = f.state();
      r.timeS = t;
      r.finalPos = pos;
      if (r.end != FollowState::FINISHED) {
        const FollowStatus s = f.status();
        printf("    [sim] %s at t=%.1fs: true=(%.1f,%.1f) measured=(%.1f,%.1f) wp=%d/%d xte=%.2f distWp=%.2f\n",
               followStateName(r.end), t, pos.x, pos.y, meas.x, meas.y, s.wpIndex, s.wpCount, s.xteM, s.distToWpM);
      }
      return r;
    }
    const double targetOmega = c.turn * fp.turnFullDps;
    omega += (targetOmega - omega) * (dt / 0.2);  // 0.2 s actuator/plant lag
    const double v = c.throttle * 1.4;            // 1.4 m/s at full throttle (EDD: 1.42 m/s rated)
    heading = wrap360(heading + omega * dt);
    pos.x += v * sin(heading * 3.14159265358979 / 180.0) * dt;
    pos.y += v * cos(heading * 3.14159265358979 / 180.0) * dt;
    r.maxXte = fmax(r.maxXte, trueXte(path, pos));
    r.timeS = t;
  }
  r.end = f.state();
  r.finalPos = pos;
  return r;
}

// A started follower on a straight north-going path, for the unit-level tests.
struct Fixture {
  PathFollower f;
  Projector pr;
  explicit Fixture(double length = 100, FollowParams p = FollowParams()) {
    const char* err;
    auto ll = fromXY(densify({{0, 0}, {0, length}}, 1.5));
    CHECK(f.load(ll.data(), ll.size(), p, &err));
    pr.init(ll[0]);
    f.start(pr.toLatLon({0, 0}));
  }
  FollowCmd step(XY at, bool newFix, bool posValid, double heading, bool headingValid = true, double rate = 0) {
    return f.step(pr.toLatLon(at), newFix, posValid, heading, headingValid, rate, 0.05);
  }
};

}  // namespace

// ---------------------------------------------------------------- geo ----

TEST(geo_wrap) {
  CHECK_NEAR(wrap180(190), -170, 1e-9);
  CHECK_NEAR(wrap180(-190), 170, 1e-9);
  CHECK_NEAR(wrap180(180), 180, 1e-9);
  CHECK_NEAR(wrap180(720), 0, 1e-9);
  CHECK_NEAR(wrap360(-10), 350, 1e-9);
  CHECK_NEAR(wrap360(360), 0, 1e-9);
  CHECK_NEAR(wrap180(NAN), 0, 1e-9);
}

TEST(geo_haversine_and_bearing_known_values) {
  // One degree of latitude is ~111.195 km on the mean-radius sphere.
  CHECK_NEAR(haversineM({0, 0}, {1, 0}), 111194.9, 5.0);
  CHECK_NEAR(haversineM({0, 0}, {0, 1}), 111194.9, 5.0);
  CHECK_NEAR(haversineM(kOrigin, kOrigin), 0, 1e-9);
  CHECK_NEAR(bearingDeg({0, 0}, {1, 0}), 0, 1e-6);     // due north
  CHECK_NEAR(bearingDeg({0, 0}, {0, 1}), 90, 1e-6);    // due east
  CHECK_NEAR(bearingDeg({0, 0}, {-1, 0}), 180, 1e-6);  // due south
  CHECK_NEAR(bearingDeg({0, 0}, {0, -1}), 270, 1e-6);  // due west
}

TEST(geo_projector_matches_haversine_at_field_scale_and_round_trips) {
  Projector pr;
  pr.init(kOrigin);
  const XY p{350.0, -420.0};  // a 550 m diagonal: bigger than any field
  const LatLon ll = pr.toLatLon(p);
  const XY back = pr.toXY(ll);
  CHECK_NEAR(back.x, p.x, 1e-6);
  CHECK_NEAR(back.y, p.y, 1e-6);
  CHECK_NEAR(distXY({0, 0}, p), haversineM(kOrigin, ll), 0.1);  // < 10 cm at 550 m
}

TEST(geo_point_segment_distance) {
  const XY a{0, 0}, b{10, 0};
  CHECK_NEAR(distPointSegment({5, 3}, a, b), 3, 1e-9);
  CHECK_NEAR(distPointSegment({-4, 3}, a, b), 5, 1e-9);   // clamps to endpoint a
  CHECK_NEAR(distPointSegment({13, 4}, a, b), 5, 1e-9);   // clamps to endpoint b
  CHECK_NEAR(distPointSegment({1, 1}, a, a), hypot(1, 1), 1e-9);  // degenerate segment
}

TEST(geo_plausibility_rejects_null_island_and_garbage) {
  CHECK(latLonPlausible(31.5, 74.3));
  CHECK(!latLonPlausible(0, 0));
  CHECK(!latLonPlausible(91, 0));
  CHECK(!latLonPlausible(0, 181));
  CHECK(!latLonPlausible(NAN, 0));
}

TEST(simplify_collinear_points_collapse_to_the_endpoints) {
  auto pts = fromXY(densify({{0, 0}, {100, 0}}, 1.5));
  LatLon out[200];
  const size_t n = simplifyPath(pts.data(), pts.size(), 0.4, out, 200);
  CHECK(n == 2);
}

TEST(simplify_keeps_corners_and_respects_tolerance) {
  auto pts = fromXY(densify({{0, 0}, {50, 0}, {50, 50}}, 1.5));
  LatLon out[200];
  const size_t n = simplifyPath(pts.data(), pts.size(), 0.4, out, 200);
  CHECK(n == 3);  // start, the corner, the end
  Projector pr;
  pr.init(pts[0]);
  const XY corner = pr.toXY(out[1]);
  CHECK_NEAR(corner.x, 50, 0.1);
  CHECK_NEAR(corner.y, 0, 0.1);
}

TEST(simplify_never_writes_past_the_output_capacity) {
  auto pts = fromXY(densify({{0, 0}, {50, 0}, {50, 50}, {0, 50}, {0, 0}}, 1.5));
  LatLon out[3];
  const size_t n = simplifyPath(pts.data(), pts.size(), 0.0001, out, 3);
  CHECK(n <= 3);
}

// ------------------------------------------------------------- follow ----

TEST(follow_load_validation) {
  PathFollower f;
  const char* err = nullptr;
  FollowParams p;
  LatLon one[1] = {kOrigin};
  CHECK(!f.load(one, 1, p, &err));
  CHECK(!f.load(nullptr, 5, p, &err));

  LatLon nan2[2] = {kOrigin, {NAN, 74.0}};
  CHECK(!f.load(nan2, 2, p, &err));

  LatLon dup[3] = {kOrigin, kOrigin, kOrigin};
  CHECK(!f.load(dup, 3, p, &err));  // zero length after de-duplication

  auto tiny = fromXY({{0, 0}, {1.0, 0}});
  CHECK(!f.load(tiny.data(), tiny.size(), p, &err));  // shorter than 2 m

  auto jump = fromXY({{0, 0}, {10, 0}, {2000, 0}});
  CHECK(!f.load(jump.data(), jump.size(), p, &err));  // a 1990 m leg is a corrupt file, not a path

  auto ok = fromXY(densify({{0, 0}, {30, 0}}, 1.5));
  CHECK(f.load(ok.data(), ok.size(), p, &err));
  CHECK(f.loaded());
}

TEST(follow_enforces_offpath_limit_above_acceptance_radius) {
  FollowParams p;
  p.acceptRadiusM = 5;
  p.offPathLimitM = 5.5;  // would trip constantly while cutting corners
  PathFollower f;
  const char* err;
  auto path = fromXY(densify({{0, 0}, {40, 0}}, 1.5));
  CHECK(f.load(path.data(), path.size(), p, &err));
  Projector pr;
  pr.init(path[0]);
  f.start(pr.toLatLon({0, 0}));
  // Sustained 6.5 m off the line is tolerated: the limit was raised to accept+2 = 7 m.
  for (int i = 0; i < 20; i++) f.step(pr.toLatLon({3, 6.5}), true, true, 90, true, 0, 0.2);
  CHECK(f.state() == FollowState::RUNNING);
}

TEST(follow_steers_towards_the_target_with_the_right_sign) {
  Fixture x;
  FollowCmd straight = x.step({0, 0}, true, true, 0);  // heading north, path north: on course
  CHECK(straight.throttle > 0.5f);
  CHECK(fabsf(straight.turn) < 0.01f);

  FollowCmd west = x.step({0, 0}, false, true, 270);  // pointing west -> target is to the right
  CHECK(west.turn > 0.05f);
  FollowCmd east = x.step({0, 0}, false, true, 90);   // pointing east -> target is to the left
  CHECK(east.turn < -0.05f);
}

TEST(follow_pivots_in_place_when_far_off_heading) {
  Fixture x;
  FollowCmd c = x.step({0, 0}, true, true, 180);  // pointing backwards
  CHECK(c.throttle == 0.0f);
  CHECK(fabsf(c.turn) > 0.2f);
}

TEST(follow_never_reverses_and_stays_in_range) {
  Fixture x;
  for (int h = 0; h < 360; h += 7) {
    FollowCmd c = x.step({0, 0}, (h % 2) == 0, true, h, true, (h % 3) * 40.0 - 40.0);
    CHECK(c.throttle >= 0.0f && c.throttle <= 1.0f);
    CHECK(c.turn >= -1.0f && c.turn <= 1.0f);
  }
}

TEST(follow_straight_line_completes_with_small_error_noise_free) {
  FollowParams p;
  auto path = densify({{0, 0}, {0, 120}}, 1.5);
  SimResult r = simulate(path, SimOptions(), p);
  CHECK(r.end == FollowState::FINISHED);
  CHECK(r.maxXte < 0.5);
  CHECK(distXY(r.finalPos, path.back()) < p.acceptRadiusM + 0.5);
}

TEST(follow_survives_gps_noise_on_an_l_shaped_path) {
  FollowParams p;
  auto path = densify({{0, 0}, {0, 80}, {80, 80}}, 1.5);
  SimOptions o;
  o.gpsSigma = 1.0;
  SimResult r = simulate(path, o, p);
  CHECK(r.end == FollowState::FINISHED);
  CHECK(r.maxXte < 4.0);
  printf("    L-path, GPS sigma 1 m: finished in %.0f s, max true XTE %.2f m\n", r.timeS, r.maxXte);
}

TEST(follow_tolerates_a_constant_heading_bias) {
  FollowParams p;
  auto path = densify({{0, 0}, {0, 100}}, 1.5);
  SimOptions o;
  o.gpsSigma = 0.3;
  o.headingBiasDeg = 8.0;  // 8 degrees of uncorrected gyro drift
  SimResult r = simulate(path, o, p);
  CHECK(r.end == FollowState::FINISHED);
  CHECK(r.maxXte < p.offPathLimitM);
}

TEST(follow_handles_an_s_curve_and_a_u_turn) {
  FollowParams p;
  SimOptions o;
  o.gpsSigma = 0.8;
  auto s = densify({{0, 0}, {0, 30}, {25, 55}, {25, 90}, {0, 115}, {0, 150}}, 1.5);
  SimResult a = simulate(s, o, p);
  CHECK(a.end == FollowState::FINISHED);
  auto u = densify({{0, 0}, {0, 60}, {8, 60}, {8, 0}}, 1.5);  // 8 m wide U-turn at the end of a row
  SimResult b = simulate(u, o, p);
  CHECK(b.end == FollowState::FINISHED);
  printf("    S-curve: %.0f s, max XTE %.2f m; U-turn: %.0f s, max XTE %.2f m\n", a.timeS, a.maxXte, b.timeS, b.maxXte);
}

// Regression for the bug found by this very simulation: one bad GPS sample used to skip
// waypoints and trip OFF_PATH while the rover was actually on the line.
TEST(follow_does_not_trip_on_noise_around_a_sharp_corner) {
  FollowParams p;
  auto path = densify({{0, 0}, {0, 60}, {60, 60}}, 1.5);
  for (double sigma : {0.5, 1.0, 1.5, 2.0}) {
    SimOptions o;
    o.gpsSigma = sigma;
    SimResult r = simulate(path, o, p);
    printf("    corner, GPS sigma %.1f m: end=%s t=%.0fs maxXTE=%.2f\n", sigma, followStateName(r.end), r.timeS, r.maxXte);
    CHECK(r.end == FollowState::FINISHED);
  }
}

TEST(follow_rides_out_multipath_outliers) {
  // 3% of fixes are 12 m multipath jumps, on top of 1 m noise. The rover must not stop or wander.
  FollowParams p;
  auto path = densify({{0, 0}, {0, 70}, {50, 70}, {50, 0}}, 1.5);
  SimOptions o;
  o.gpsSigma = 1.0;
  o.outlierProb = 0.03;
  SimResult r = simulate(path, o, p);
  CHECK(r.end == FollowState::FINISHED);
  CHECK(r.maxXte < 5.0);
  printf("    outliers: finished in %.0f s, max true XTE %.2f m\n", r.timeS, r.maxXte);
}

TEST(follow_closed_loop_does_not_finish_immediately) {
  // Start and end are the same place. The rover must drive the whole loop, not stop at once.
  FollowParams p;
  auto loop = densify({{0, 0}, {0, 40}, {40, 40}, {40, 0}, {0, 0}}, 1.5);
  PathFollower f;
  const char* err;
  auto ll = fromXY(loop);
  CHECK(f.load(ll.data(), ll.size(), p, &err));
  Projector pr;
  pr.init(ll[0]);
  f.start(pr.toLatLon({0, 0}));
  for (int i = 0; i < 6; i++) f.step(pr.toLatLon({0, 0}), true, true, 0, true, 0, 0.05);
  CHECK(f.state() == FollowState::RUNNING);
  CHECK(f.status().wpIndex < 10);  // consumed only the first few points, not all the way round
  SimOptions o;
  o.gpsSigma = 0.5;
  SimResult r = simulate(loop, o, p);
  CHECK(r.end == FollowState::FINISHED);
  CHECK(r.timeS > 60.0);  // a 160 m loop at ~0.84 m/s cannot finish in under a minute
}

TEST(follow_consumes_at_most_one_waypoint_per_fix) {
  Fixture x(100);
  const int before = x.f.status().wpIndex;
  x.step({0, 40}, true, true, 0);  // a single wild sample 40 m up the path
  CHECK(x.f.status().wpIndex <= before + 1);
}

TEST(follow_a_single_outlier_fix_is_ignored) {
  Fixture x(200);
  for (int i = 0; i < 4; i++) x.step({0, 1.0 + i}, true, true, 0);
  x.step({40, 8}, true, true, 0);  // one multipath jump
  x.step({0, 6}, true, true, 0);
  x.step({0, 7}, true, true, 0);
  CHECK(x.f.state() == FollowState::RUNNING);
}

TEST(follow_faults_off_path_when_far_from_the_path_at_start) {
  FollowParams p;
  auto path = densify({{0, 0}, {0, 100}}, 1.5);
  SimOptions o;
  o.maxTimeS = 60;
  o.startOffsetX = 40.0;
  SimResult r = simulate(path, o, p);
  CHECK(r.end == FollowState::FAULT_OFFPATH);
}

TEST(follow_faults_off_path_when_really_displaced_mid_run) {
  Fixture x(200);
  x.step({0, 0.5}, true, true, 0);
  CHECK(x.f.state() == FollowState::RUNNING);
  // Pushed 20 m sideways and staying there: a few consecutive bad fixes are needed.
  for (int i = 0; i < 6; i++) x.step({20, 4}, true, true, 0);
  CHECK(x.f.state() == FollowState::FAULT_OFFPATH);
  FollowCmd c = x.step({20, 4}, true, true, 0);
  CHECK(c.throttle == 0.0f && c.turn == 0.0f);  // a faulted follower outputs nothing
}

TEST(follow_a_sustained_gps_teleport_is_a_fault_not_a_silent_success) {
  Fixture x(200);
  x.step({0, 0.5}, true, true, 0);
  for (int i = 0; i < 6; i++) x.step({0, 120}, true, true, 0);  // 120 m in a second: not credible
  CHECK(x.f.state() == FollowState::FAULT_OFFPATH);
}

TEST(follow_no_fix_grace_then_fault) {
  Fixture x;
  for (int i = 0; i < 20; i++) {  // 1.0 s without a fix: tolerated, but the rover holds still
    FollowCmd c = x.step({0, 0}, false, false, 0);
    CHECK(c.throttle == 0.0f);
  }
  CHECK(x.f.state() == FollowState::FAULT_NOFIX == false);  // still running
  x.step({0, 0}, true, true, 0);                            // the fix returns: the timer resets
  for (int i = 0; i < 20; i++) x.step({0, 0}, false, false, 0);
  CHECK(x.f.state() == FollowState::RUNNING);
  for (int i = 0; i < 20; i++) x.step({0, 0}, false, false, 0);  // now 2 s in a row
  CHECK(x.f.state() == FollowState::FAULT_NOFIX);
}

TEST(follow_faults_without_heading) {
  Fixture x;
  x.step({0, 0}, true, true, 0, /*headingValid=*/false);
  CHECK(x.f.state() == FollowState::FAULT_NOHEADING);
}

TEST(follow_faults_stuck_when_the_rover_does_not_move) {
  FollowParams p;
  p.stuckTimeoutS = 10;
  Fixture x(100, p);
  int steps = 0;
  while (x.f.state() == FollowState::RUNNING && steps < 2000) {  // wheels spinning, going nowhere
    x.step({0, 0}, (steps % 4) == 0, true, 0);
    steps++;
  }
  CHECK(x.f.state() == FollowState::FAULT_STUCK);
  CHECK(steps * 0.05 > 9.0 && steps * 0.05 < 12.0);
}

TEST(the_example_path_in_the_repo_is_valid_and_followable) {
  FILE* f = fopen("paths/example_square.csv", "r");  // tests run from the repo root
  if (!f) f = fopen("../paths/example_square.csv", "r");
  CHECK(f != nullptr);
  if (!f) return;
  std::vector<LatLon> pts;
  char line[128];
  while (fgets(line, sizeof(line), f)) {
    double la, lo;
    if (sscanf(line, "%lf,%lf", &la, &lo) == 2) pts.push_back({la, lo});
  }
  fclose(f);
  CHECK(pts.size() > 20);
  PathFollower fol;
  const char* err = nullptr;
  CHECK(fol.load(pts.data(), pts.size(), FollowParams(), &err));
  CHECK_NEAR(fol.totalLengthM(), 80.0, 1.0);  // a 20 m square
  // Closed loop: start and end coincide, and the follower must still treat it as a path to drive.
  Projector pr;
  pr.init(pts[0]);
  std::vector<XY> xy;
  for (const LatLon& p : pts) xy.push_back(pr.toXY(p));
  SimOptions o;
  o.gpsSigma = 0.8;
  SimResult r = simulate(xy, o, FollowParams());
  CHECK(r.end == FollowState::FINISHED);
  printf("    example square, GPS sigma 0.8 m: finished in %.0f s, max XTE %.2f m\n", r.timeS, r.maxXte);
}

TEST(follow_resumes_from_the_nearest_segment) {
  FollowParams p;
  auto ll = fromXY(densify({{0, 0}, {0, 150}}, 1.5));
  PathFollower f;
  const char* err;
  CHECK(f.load(ll.data(), ll.size(), p, &err));
  Projector pr;
  pr.init(ll[0]);
  f.start(pr.toLatLon({1.0, 75.0}));  // restarted halfway along, slightly off the line
  CHECK(f.state() == FollowState::RUNNING);
  CHECK(f.status().wpIndex > 40 && f.status().wpIndex < 60);
}
