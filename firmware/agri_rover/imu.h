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
