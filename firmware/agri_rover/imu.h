#pragma once
// MPU6050 on I2C: gyro-bias calibration, tilt, and a compass heading.
//
// There is no magnetometer, so heading is gyro-integrated yaw. It starts "unaligned"; the first
// time the rover drives straight above IMU_GPS_ALIGN_MIN_SPEED the GPS course over ground sets
// it, and later GPS courses keep nudging it so gyro drift cannot accumulate. Everything here is
// called ONLY from the control task (one owner of the I2C bus), apart from imuBegin().
#include <Arduino.h>

#include "state.h"

bool imuBegin();  // probes + configures the chip, loads the saved bias; starts a calibration
// Call every control tick with the elapsed time. Reads the sensor and updates the estimate.
void imuUpdate(float dt);
// Called at ~5 Hz when the GPS reports a trustworthy course and speed.
void imuFeedGpsCourse(float courseDeg, float speedMps);
// Ask for a new gyro-bias calibration (rover must be still for ~3 s). Safe to call from any task.
void imuRequestCalibration();
ImuData imuGet();
