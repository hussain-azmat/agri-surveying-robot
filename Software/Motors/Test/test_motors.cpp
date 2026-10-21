// Host unit tests: Motors (skid-steer mixer).
#include <math.h>
#include "mini_test.h"
#include "motors.h"

// ------------------------------------------------------------ mixer ----

TEST(mixer_straight_pivot_and_turn) {
  WheelSpeeds s = mixSkidSteer(0.5f, 0.0f);
  CHECK(s.fl == 0.5f && s.fr == 0.5f && s.rl == 0.5f && s.rr == 0.5f);
  WheelSpeeds p = mixSkidSteer(0.0f, 0.6f);  // pivot clockwise: left forward, right backward
  CHECK(p.fl > 0 && p.rl > 0 && p.fr < 0 && p.rr < 0);
  CHECK_NEAR(p.fl, -p.fr, 1e-6);
  WheelSpeeds t = mixSkidSteer(0.5f, 0.2f);  // gentle right turn: left side faster
  CHECK(t.fl > t.fr);
}

TEST(mixer_normalises_instead_of_clipping_one_side) {
  WheelSpeeds s = mixSkidSteer(1.0f, 1.0f);  // would be left=2, right=0
  CHECK(fabsf(s.fl) <= 1.0f && fabsf(s.fr) <= 1.0f);
  CHECK_NEAR(s.fl, 1.0, 1e-6);
  CHECK_NEAR(s.fr, 0.0, 1e-6);
  WheelSpeeds q = mixSkidSteer(0.9f, 0.6f);  // left 1.5, right 0.3 -> scaled together, ratio kept
  CHECK_NEAR(q.fr / q.fl, 0.3 / 1.5, 1e-5);
}

TEST(mixer_clamps_wild_inputs_and_applies_the_limit) {
  WheelSpeeds s = mixSkidSteer(50.0f, -50.0f, 0.5f);
  CHECK(fabsf(s.fl) <= 0.5f + 1e-6f && fabsf(s.fr) <= 0.5f + 1e-6f);
  WheelSpeeds z = mixSkidSteer(NAN, NAN);  // garbage in -> stopped out
  CHECK(z.fl == 0.0f && z.fr == 0.0f && z.rl == 0.0f && z.rr == 0.0f);
  WheelSpeeds i = mixSkidSteer(INFINITY, 0.0f);
  CHECK(i.fl == 0.0f && i.fr == 0.0f);
}
