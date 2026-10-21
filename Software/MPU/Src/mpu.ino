// =============================================================================
// MPU bench: runs ONLY the IMU (MPU6050), nothing else. Use it to prove the sensor, the wiring and the pins
// before anything moves. Flash it, open the serial monitor at 115200.
//
//   {"status":"INFO","message":"IMU I2C pins: SDA=GPIO21 SCL=GPIO22 ..."}   <- which pins, and did the chip answer
//   {"imu":{"accel_x":...,"gyro_z":...}}                                       <- raw readings every 500 ms
//   {"mpu":{"ok":true,"calibrated":true,"pitch":0.4,"roll":-0.2,"tilt":0.5,"gyro_noise_dps":0.04}}
//
// PASS (see Software/MPU/Readme.txt): ok=true, calibrated=true within ~5 s, flat and still => accel_z about 9.8,
// gyro values about 0, tilt below 2 degrees. Tilt the rover and tilt/pitch/roll must follow. Keep it STILL for
// the first ~3 s (that is the gyro calibration).
// =============================================================================
#include "config.h"
#include "github_log.h"
#include "imu.h"

static uint32_t g_lastUs = 0;
static uint32_t g_lastPrintMs = 0;

void setup() {
  Serial.begin(115200);
  delay(300);
  serialLog("INFO", "MPU bench v" FW_VERSION ": only the IMU runs. Keep the rover still for 3 s (gyro calibration).");
  if (!imuBegin()) serialLog("ERROR", "IMU did not start: check the pins and wiring (see messages above)");
  g_lastUs = micros();
}

void loop() {
  const uint32_t now = micros();
  const float dt = (now - g_lastUs) * 1e-6f;
  g_lastUs = now;
  imuUpdate(dt);

  if (millis() - g_lastPrintMs >= 500) {
    g_lastPrintMs = millis();
    const ImuData d = imuGet();
    char line[300];
    formatImuJson(line, sizeof(line), d);
    Serial.println(line);
    snprintf(line, sizeof(line),
             "{\"mpu\":{\"ok\":%s,\"calibrated\":%s,\"pitch\":%.1f,\"roll\":%.1f,\"tilt\":%.1f,\"gyro_noise_dps\":%.3f,\"errors\":%lu}}",
             d.ok ? "true" : "false", d.calibrated ? "true" : "false", d.pitchDeg, d.rollDeg, d.tiltDeg, d.gyroNoiseDps,
             (unsigned long)d.errors);
    Serial.println(line);
  }
  delay(10);  // about 100 Hz, as in the real control task
}
