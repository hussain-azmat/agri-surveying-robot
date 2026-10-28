#pragma once
// IMU: MPU6050 on I2C, gyro-bias calibration, tilt, and a compass heading.
#include <stdint.h>

struct ImuData {
  bool ok = false;           // sensor answering
  bool calibrated = false;   // gyro bias known
  bool calibrating = false;
  bool yawValid = false;     // heading aligned to the world (needs a GPS course once)
  float yawDeg = 0;          // compass heading, clockwise from north
  float yawRateDps = 0;      // clockwise positive
  float pitchDeg = 0, rollDeg = 0, tiltDeg = 0;
  float gyroNoiseDps = 0;    // short-term gyro noise: watch it rise when the motors run (EDD stage 0)
  uint32_t errors = 0;
  // Raw sensor readings, no bias removed: what the Adafruit library returned in the original sketch.
  float accelX = 0, accelY = 0, accelZ = 0;  // m/s^2
  float gyroX = 0, gyroY = 0, gyroZ = 0;     // rad/s
};

// ---- Pure helpers (plain C++, unit-tested on a laptop) --------------------------------------------
#include <math.h>

// The control task reads the IMU at 100 Hz, but a log row is written only every GPS fix (5 Hz or slower). A single
// reading per row would alias vibration and miss fast gyro changes, so the readings between two rows are averaged.
struct ImuMeans {
  uint32_t n = 0;                 // readings averaged (0 = none, e.g. the IMU was not answering)
  float ax = 0, ay = 0, az = 0;   // mean raw accel, m/s^2
  float gx = 0, gy = 0, gz = 0;   // mean raw gyro, rad/s
  float gyroPeak = 0;             // largest gyro vector length seen in the window, rad/s
};

struct ImuWindow {
  uint32_t n = 0;
  double sax = 0, say = 0, saz = 0, sgx = 0, sgy = 0, sgz = 0;
  float peak2 = 0;  // largest gx^2+gy^2+gz^2

  void add(const ImuData& d) {
    if (!d.ok) return;  // a dead sensor contributes nothing (the row then shows empty IMU fields)
    n++;
    sax += d.accelX; say += d.accelY; saz += d.accelZ;
    sgx += d.gyroX; sgy += d.gyroY; sgz += d.gyroZ;
    const float g2 = d.gyroX * d.gyroX + d.gyroY * d.gyroY + d.gyroZ * d.gyroZ;
    if (g2 > peak2) peak2 = g2;
  }
  // Returns the means and starts a new window.
  ImuMeans take() {
    ImuMeans m;
    m.n = n;
    if (n) {
      m.ax = (float)(sax / n); m.ay = (float)(say / n); m.az = (float)(saz / n);
      m.gx = (float)(sgx / n); m.gy = (float)(sgy / n); m.gz = (float)(sgz / n);
      m.gyroPeak = sqrtf(peak2);
    }
    *this = ImuWindow();
    return m;
  }
};

// How far the rover has veered from a straight line while NOT being told to turn.
//
// Fed every control tick with the yaw rate the gyro measures and the throttle / turn commands actually applied. A
// "straight run" is: moving (|throttle| >= 0.1) with no turn command (|turn| <= 0.02), IMU answering. During a run the
// gyro yaw rate is integrated, so driftDeg() is the exact angle the rover has turned since the run began, whatever the
// reason (unequal motors, a slipping wheel, a bump, a slope) and independent of any GPS heading correction.
// Positive = veered clockwise (to the right). The first SETTLE_S of a run are skipped, because the rover is still
// finishing the previous turn or accelerating; the drift is reported once the run has lasted MIN_S.
// Accuracy: the gyro's residual bias, typically a few hundredths of a degree per second.
struct StraightMonitor {
  static constexpr float SETTLE_S = 0.5f;
  static constexpr float MIN_S = 1.0f;
  static constexpr float MAX_TURN = 0.02f;
  static constexpr float MIN_THROTTLE = 0.1f;

  void update(float dt, float yawRateDps, float cmdThrottle, float cmdTurn, bool imuOk) {
    const bool straight = imuOk && isfinite(yawRateDps) && isfinite(cmdThrottle) && isfinite(cmdTurn) &&
                          fabsf(cmdThrottle) >= MIN_THROTTLE && fabsf(cmdTurn) <= MAX_TURN;
    if (!straight) {
      runS_ = 0;
      drift_ = 0;
      return;
    }
    runS_ += dt;
    if (runS_ > SETTLE_S) drift_ += yawRateDps * dt;
  }
  bool valid() const { return runS_ >= MIN_S; }
  float driftDeg() const { return valid() ? drift_ : 0.0f; }
  float straightS() const { return runS_; }

 private:
  float runS_ = 0;
  float drift_ = 0;
};

#ifdef ARDUINO
#include <Arduino.h>

// MPU6050 on I2C: gyro-bias calibration, tilt, and a compass heading.
//
// There is no magnetometer, so heading is gyro-integrated yaw. It starts "unaligned"; the first
// time the rover drives straight above IMU_GPS_ALIGN_MIN_SPEED the GPS course over ground sets
// it, and later GPS courses keep nudging it so gyro drift cannot accumulate. Everything here is
// called ONLY from the control task (one owner of the I2C bus), apart from imuBegin().


bool imuBegin();  // probes + configures the chip, loads the saved bias; starts a calibration
// Call every control tick with the elapsed time. Reads the sensor and updates the estimate.
void imuUpdate(float dt);
// Called at ~5 Hz when the GPS reports a trustworthy course and speed.
void imuFeedGpsCourse(float courseDeg, float speedMps);
// Ask for a new gyro-bias calibration (rover must be still for ~3 s). Safe to call from any task.
void imuRequestCalibration();
ImuData imuGet();
#endif
