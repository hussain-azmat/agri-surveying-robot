#include "motors.h"

#include <math.h>

#include "mixer.h"

#if !defined(ESP_ARDUINO_VERSION_MAJOR) || ESP_ARDUINO_VERSION_MAJOR < 3
#error "This firmware needs the arduino-esp32 core 3.x (LEDC pin-based API). Tested with 3.1.3."
#endif

namespace {
float g_target[W_COUNT] = {0, 0, 0, 0};   // requested speed per wheel, -1..1
float g_applied[W_COUNT] = {0, 0, 0, 0};  // what the pins currently output (after slew limiting)
bool g_ready = false;

constexpr uint32_t kMaxDuty = (1u << MOTOR_PWM_BITS) - 1;

// Maps a speed in [-1, 1] to a duty. Below the dead-zone threshold the GA25-370 would only hum
// and heat, so tiny commands are treated as zero and everything above is rescaled to start at
// MOTOR_MIN_DUTY.
uint32_t speedToDuty(float speedAbs) {
  if (speedAbs < 0.02f) return 0;
  float d = MOTOR_MIN_DUTY + (1.0f - MOTOR_MIN_DUTY) * fminf(speedAbs, 1.0f);
  return (uint32_t)(d * kMaxDuty + 0.5f);
}

void writeWheel(int w, float speed) {
  if (!g_ready) return;
  if (WHEEL_INVERT[w]) speed = -speed;
  const uint32_t duty = speedToDuty(fabsf(speed));
  // Never energise both inputs: always zero the idle side first (no shoot-through even for a moment).
  if (speed >= 0) {
    ledcWrite(PIN_MOTOR_B[w], 0);
    ledcWrite(PIN_MOTOR_A[w], duty);
  } else {
    ledcWrite(PIN_MOTOR_A[w], 0);
    ledcWrite(PIN_MOTOR_B[w], duty);
  }
}
}  // namespace

void motorsBegin() {
  // Step 1: plain GPIO, LOW. This is the "motors off at boot" guarantee.
  for (int w = 0; w < W_COUNT; w++) {
    pinMode(PIN_MOTOR_A[w], OUTPUT);
    digitalWrite(PIN_MOTOR_A[w], LOW);
    pinMode(PIN_MOTOR_B[w], OUTPUT);
    digitalWrite(PIN_MOTOR_B[w], LOW);
  }
  // Step 2: hand the pins to the PWM peripheral, starting at duty 0.
  for (int w = 0; w < W_COUNT; w++) {
    ledcAttach(PIN_MOTOR_A[w], MOTOR_PWM_HZ, MOTOR_PWM_BITS);
    ledcAttach(PIN_MOTOR_B[w], MOTOR_PWM_HZ, MOTOR_PWM_BITS);
    ledcWrite(PIN_MOTOR_A[w], 0);
    ledcWrite(PIN_MOTOR_B[w], 0);
  }
  g_ready = true;
  motorsStop();
}

void motorsSetWheels(const float target[W_COUNT]) {
  for (int w = 0; w < W_COUNT; w++) {
    float v = target[w];
    if (!isfinite(v)) v = 0;  // a NaN must never become full speed
    g_target[w] = constrain(v, -1.0f, 1.0f) * WHEEL_TRIM[w];
  }
}

void motorsDrive(float throttle, float turn, float limit) {
  const WheelSpeeds s = mixSkidSteer(throttle, turn, limit);
  const float t[W_COUNT] = {s.fl, s.fr, s.rl, s.rr};
  motorsSetWheels(t);
}

void motorsStop() {
  for (int w = 0; w < W_COUNT; w++) {
    g_target[w] = 0;
    g_applied[w] = 0;
    writeWheel(w, 0);
  }
}

void motorsUpdate(float dt) {
  if (!(dt > 0) || dt > 0.2f) dt = 0.01f;
  const float step = MOTOR_RAMP_PER_S * dt;
  for (int w = 0; w < W_COUNT; w++) {
    const float diff = g_target[w] - g_applied[w];
    // Ramp only when speeding up; a reduction toward zero is applied at 3x the rate so a "stop"
    // from a joystick is crisp but never an instant current spike from full-speed reversal.
    const bool slowing = fabsf(g_target[w]) < fabsf(g_applied[w]);
    const float lim = slowing ? step * 3.0f : step;
    if (fabsf(diff) <= lim) g_applied[w] = g_target[w];
    else g_applied[w] += diff > 0 ? lim : -lim;
    writeWheel(w, g_applied[w]);
  }
}

bool motorsStopped() {
  for (int w = 0; w < W_COUNT; w++) {
    if (g_applied[w] != 0.0f || g_target[w] != 0.0f) return false;
  }
  return true;
}

void motorsGetApplied(float out[W_COUNT]) {
  for (int w = 0; w < W_COUNT; w++) out[w] = g_applied[w];
}
