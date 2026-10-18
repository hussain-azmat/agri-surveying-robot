#pragma once
// PURE: no Arduino dependencies.
//
// Skid-steer mixing. `throttle` and `turn` are in [-1, 1]; positive turn = clockwise
// (a right turn). The left wheels run faster than the right ones to turn right.
// If the sum would exceed full scale, both sides are scaled down together so the turn
// radius is preserved instead of one side clipping.
#include <math.h>

struct WheelSpeeds {
  float fl = 0, fr = 0, rl = 0, rr = 0;  // -1..1
};

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

inline WheelSpeeds mixSkidSteer(float throttle, float turn, float limit = 1.0f) {
  // A NaN/inf command (a sensor or parsing bug upstream) must mean "stop", never "full speed".
  if (!isfinite(throttle)) throttle = 0.0f;
  if (!isfinite(turn)) turn = 0.0f;
  if (!isfinite(limit)) limit = 0.0f;
  throttle = clampf(throttle, -1.0f, 1.0f);
  turn = clampf(turn, -1.0f, 1.0f);
  float left = throttle + turn;
  float right = throttle - turn;
  const float m = fmaxf(fabsf(left), fabsf(right));
  if (m > 1.0f) {
    left /= m;
    right /= m;
  }
  limit = clampf(limit, 0.0f, 1.0f);
  WheelSpeeds w;
  w.fl = w.rl = left * limit;
  w.fr = w.rr = right * limit;
  return w;
}
