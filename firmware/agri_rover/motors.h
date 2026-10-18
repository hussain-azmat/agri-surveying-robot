#pragma once
// Four independent motor drivers (one L298N channel per wheel), PWM speed control,
// skid-steer mixing, slew-rate limiting and an immediate stop.
//
// SAFETY: motorsBegin() is the FIRST thing setup() calls. It forces every driver input LOW
// before anything else (WiFi, flash, sensors) can run. For the short time before setup()
// starts, the pins float: fit 10k pull-down resistors on the 8 driver inputs (docs/PINOUT.md).
#include <Arduino.h>

#include "config.h"

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
