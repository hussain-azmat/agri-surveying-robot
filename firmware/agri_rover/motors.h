#pragma once
// Motors: four independent L298N channels (IN1/IN2 = direction, EN = PWM speed), skid-steer mixing,
// slew-rate limiting and an immediate stop.
//
// The top half (mixSkidSteer) is plain C++ and unit-tested on a laptop; the part under #ifdef ARDUINO drives the pins.
#include <math.h>
#include "config.h"

//
// Skid-steer mixing. `throttle` and `turn` are in [-1, 1]; positive turn = clockwise
// (a right turn). The left wheels run faster than the right ones to turn right.
// If the sum would exceed full scale, both sides are scaled down together so the turn
// radius is preserved instead of one side clipping.

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

#ifdef ARDUINO
#include <Arduino.h>

// Four independent motor drivers (one L298N module per wheel: IN1/IN2 = direction, EN = PWM speed),
// skid-steer mixing, slew-rate limiting and an immediate stop.
//
// SAFETY: motorsBegin() is the FIRST thing setup() calls. It forces every EN and IN pin LOW
// before anything else (WiFi, flash, sensors) can run. For the short time before setup()
// starts, the pins float: fit 10k pull-down resistors on the 4 EN pins, which gate the drivers
// (docs/PINOUT.md).


void motorsBegin();

// Skid-steer drive: throttle and turn in [-1, 1] (turn > 0 = clockwise/right), scaled by `limit`.
// Takes effect through the slew limiter in motorsUpdate().
void motorsDrive(float throttle, float turn, float limit = 1.0f);
// Direct per-wheel targets in [-1, 1] (FL, FR, RL, RR). Used by drive and by the bench test.
void motorsSetWheels(const float target[W_COUNT]);
// Immediate stop: targets AND outputs go to zero this instant (no ramp).
void motorsStop();
// Apply targets with slew-rate limiting. Call every control tick with the real elapsed time.
void motorsUpdate(float dt);

bool motorsStopped();
void motorsGetApplied(float out[W_COUNT]);
#endif
