#include "imu.h"

#ifdef ARDUINO
#include <Preferences.h>
#include <Wire.h>
#include <math.h>

#include "github_log.h"
#include "path.h"

namespace {

// The gyro bias is remembered in flash, so a rover that boots while being moved still has a good value.
Preferences& nvs() {
  static Preferences p;
  static bool opened = false;
  if (!opened) opened = p.begin("imu", false);
  return p;
}
bool biasLoad(float b[3]) {
  if (nvs().getBytesLength("bias") != sizeof(float) * 3) return false;
  nvs().getBytes("bias", b, sizeof(float) * 3);
  for (int i = 0; i < 3; i++) {
    if (!isfinite(b[i]) || fabsf(b[i]) > 20.0f) return false;  // implausible: do not trust it
  }
  return true;
}
void biasSave(const float b[3]) { nvs().putBytes("bias", b, sizeof(float) * 3); }


// MPU6050 registers
constexpr uint8_t REG_SMPLRT_DIV = 0x19, REG_CONFIG = 0x1A, REG_GYRO_CFG = 0x1B, REG_ACCEL_CFG = 0x1C;
constexpr uint8_t REG_ACCEL_XOUT = 0x3B, REG_PWR_MGMT_1 = 0x6B, REG_WHO_AM_I = 0x75;
constexpr float GYRO_LSB_PER_DPS = 65.5f;    // +-500 dps
constexpr float ACCEL_LSB_PER_G = 8192.0f;   // +-4 g

constexpr int CAL_SAMPLES = 300;             // ~3 s at 100 Hz
constexpr float CAL_MAX_GYRO_STD = 1.0f;     // dps: more than this means the rover was moving
constexpr float CAL_MAX_GYRO_MEAN = 12.0f;   // dps: a bias this large is a broken sensor, not a bias
constexpr float CAL_MAX_ACCEL_ERR_G = 0.2f;  // |a| should be 1 g when still

ImuData g_imu;
float g_bias[3] = {0, 0, 0};
float g_pitch = 0, g_roll = 0, g_yaw = 0;
float g_tiltLp = 0;
float g_rateLp = 0;
float g_noise = 0;
uint32_t g_lastGoodMs = 0, g_lastRecoverMs = 0;
int g_failStreak = 0;
bool g_present = false;
volatile bool g_calRequested = false;

// Calibration accumulator
bool g_cal = false;
int g_calN = 0;
double g_calSum[3], g_calSumSq[3], g_calAccN = 0;

int g_alignRejects = 0;

bool writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(IMU_I2C_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

bool readRegs(uint8_t reg, uint8_t* buf, size_t n) {
  Wire.beginTransmission(IMU_I2C_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)IMU_I2C_ADDR, (int)n) != (int)n) return false;
  for (size_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

// A motor-noise glitch can leave the MPU6050 holding SDA low. Clock SCL until it lets go.
void i2cBusClear() {
  Wire.end();
  pinMode(PIN_I2C_SCL, OUTPUT_OPEN_DRAIN);
  pinMode(PIN_I2C_SDA, INPUT_PULLUP);
  for (int i = 0; i < 9 && digitalRead(PIN_I2C_SDA) == LOW; i++) {
    digitalWrite(PIN_I2C_SCL, LOW);
    delayMicroseconds(5);
    digitalWrite(PIN_I2C_SCL, HIGH);
    delayMicroseconds(5);
  }
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, IMU_I2C_HZ);
  Wire.setTimeOut(20);
}

bool configureChip() {
  uint8_t who = 0;
  if (!readRegs(REG_WHO_AM_I, &who, 1)) return false;
  // 0x68 = MPU6050; 0x70/0x72 are common clones that behave identically for our registers.
  if (who != 0x68 && who != 0x70 && who != 0x72 && who != 0x98) return false;
  if (!writeReg(REG_PWR_MGMT_1, 0x01)) return false;  // wake up, gyro-X PLL clock
  delay(10);
  return writeReg(REG_SMPLRT_DIV, 9) &&   // 1 kHz / 10 = 100 Hz
         writeReg(REG_CONFIG, 3) &&       // digital low-pass ~44 Hz: tames motor vibration
         writeReg(REG_GYRO_CFG, 0x08) &&  // +-500 dps
         writeReg(REG_ACCEL_CFG, 0x08);   // +-4 g
}

void startCalibration() {
  g_cal = true;
  g_calN = 0;
  g_calAccN = 0;
  for (int i = 0; i < 3; i++) g_calSum[i] = g_calSumSq[i] = 0;
  g_imu.calibrating = true;
}

void finishCalibration() {
  g_cal = false;
  g_imu.calibrating = false;
  float mean[3], sd[3];
  for (int i = 0; i < 3; i++) {
    mean[i] = (float)(g_calSum[i] / g_calN);
    const double var = g_calSumSq[i] / g_calN - (double)mean[i] * mean[i];
    sd[i] = sqrtf(var > 0 ? (float)var : 0.0f);
  }
  const float accMean = (float)(g_calAccN / g_calN);
  const bool still = sd[0] < CAL_MAX_GYRO_STD && sd[1] < CAL_MAX_GYRO_STD && sd[2] < CAL_MAX_GYRO_STD;
  const bool sane = fabsf(mean[0]) < CAL_MAX_GYRO_MEAN && fabsf(mean[1]) < CAL_MAX_GYRO_MEAN &&
                    fabsf(mean[2]) < CAL_MAX_GYRO_MEAN && fabsf(accMean - 1.0f) < CAL_MAX_ACCEL_ERR_G;
  if (still && sane) {
    for (int i = 0; i < 3; i++) g_bias[i] = mean[i];
    biasSave(g_bias);
    g_imu.calibrated = true;
    serialLog("INFO", "gyro bias calibrated");
  } else {
    serialLog("WARN", "gyro calibration rejected (rover moving?); keeping the previous bias");
  }
}

}  // namespace

// Scans one I2C bus and reports every device that answers. Used only to explain a missing IMU.
int i2cScan(int sda, int scl) {
  Wire.end();
  Wire.begin(sda, scl, 100000);
  Wire.setTimeOut(20);
  int found = 0;
  for (uint8_t addr = 0x08; addr < 0x78; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      serialLog("INFO", "I2C device answers at 0x%02X with SDA=GPIO%d, SCL=GPIO%d", addr, sda, scl);
      found++;
    }
  }
  return found;
}

// Called when the MPU6050 does not answer: finds out WHY, so the pins can be confirmed without a multimeter.
void diagnoseImuPins() {
  const int here = i2cScan(PIN_I2C_SDA, PIN_I2C_SCL);
  if (here > 0) {
    serialLog("ERROR", "something answers on SDA=GPIO%d/SCL=GPIO%d but it is not an MPU6050 at 0x68 (address 0x69 means AD0 is high)",
              PIN_I2C_SDA, PIN_I2C_SCL);
  } else if (i2cScan(PIN_I2C_SCL, PIN_I2C_SDA) > 0) {  // try the two wires the other way round
    serialLog("ERROR", "the device answers only with SDA and SCL SWAPPED: swap the two IMU wires, or the pin numbers in config.h");
  } else {
    serialLog("ERROR", "no I2C device answers on SDA=GPIO%d/SCL=GPIO%d: check IMU power (3.3 V), ground, the two wires, and "
                       "PIN_I2C_SDA/PIN_I2C_SCL in config.h (is another function using one of these pins?)",
              PIN_I2C_SDA, PIN_I2C_SCL);
  }
  Wire.end();
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, IMU_I2C_HZ);
  Wire.setTimeOut(20);
}

// CONFIRM THE IMU PINS: the I2C pins are PIN_I2C_SDA / PIN_I2C_SCL in config.h (default GPIO 21 / 22, which is where every
// schematic puts the MPU6050 and what Wire.begin() uses by default). The line printed at boot says which pins this
// firmware is using; the "IMU found" line proves the chip answered on them.
bool imuBegin() {
  serialLog("INFO", "IMU I2C pins: SDA=GPIO%d SCL=GPIO%d (confirm against the wiring; see docs/PINOUT.md)", PIN_I2C_SDA,
            PIN_I2C_SCL);
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, IMU_I2C_HZ);
  Wire.setTimeOut(20);  // ms: never let a stuck bus hang the control task
  g_present = configureChip();
  g_imu.ok = g_present;
  if (biasLoad(g_bias)) g_imu.calibrated = true;  // good enough to drive until a fresh one is taken
  if (g_present) startCalibration();
  g_lastGoodMs = millis();
  if (g_present) {
    serialLog("INFO", "MPU6050 Initialized Successfully");
  } else {
    serialLog("ERROR", "MPU6050 not found! Check wiring. Heading is unavailable.");
    diagnoseImuPins();
  }
  return g_present;
}

void imuRequestCalibration() { g_calRequested = true; }
ImuData imuGet() { return g_imu; }

void imuUpdate(float dt) {
  const uint32_t now = millis();

  if (g_calRequested && !g_cal) {
    g_calRequested = false;
    startCalibration();
  }

  if (!g_present || !g_imu.ok) {
    // Try to bring a lost/absent sensor back at most once a second.
    if (now - g_lastRecoverMs > 1000) {
      g_lastRecoverMs = now;
      i2cBusClear();
      g_present = configureChip();
      if (g_present) {
        g_imu.ok = true;
        g_failStreak = 0;
        g_imu.yawValid = false;  // we were blind for a while: heading must be re-aligned
        serialLog("INFO", "IMU recovered");
      }
    }
    return;
  }

  uint8_t raw[14];
  if (!readRegs(REG_ACCEL_XOUT, raw, sizeof(raw))) {
    g_imu.errors++;
    if (++g_failStreak >= 5) {
      g_imu.ok = false;  // the safety layer sees this and stops AUTO driving
      serialLog("ERROR", "IMU lost: it stopped answering on the I2C bus");
    }
    return;
  }
  g_failStreak = 0;
  g_lastGoodMs = now;

  auto i16 = [&](int i) { return (int16_t)((raw[i] << 8) | raw[i + 1]); };
  // Chip axes: +X forward, +Y left, +Z up (right-handed). See config.h.
  const float ax = i16(0) / ACCEL_LSB_PER_G, ay = i16(2) / ACCEL_LSB_PER_G, az = i16(4) / ACCEL_LSB_PER_G;
  const float gx = i16(8) / GYRO_LSB_PER_DPS, gy = i16(10) / GYRO_LSB_PER_DPS, gz = i16(12) / GYRO_LSB_PER_DPS;

  // Raw readings for the serial JSON stream (m/s^2 and rad/s, like the Adafruit library the old sketch used).
  g_imu.accelX = ax * 9.80665f;
  g_imu.accelY = ay * 9.80665f;
  g_imu.accelZ = az * 9.80665f;
  g_imu.gyroX = gx * (float)(M_PI / 180.0);
  g_imu.gyroY = gy * (float)(M_PI / 180.0);
  g_imu.gyroZ = gz * (float)(M_PI / 180.0);

  if (g_cal) {
    const float g[3] = {gx, gy, gz};
    for (int i = 0; i < 3; i++) {
      g_calSum[i] += g[i];
      g_calSumSq[i] += (double)g[i] * g[i];
    }
    g_calAccN += sqrtf(ax * ax + ay * ay + az * az);
    if (++g_calN >= CAL_SAMPLES) finishCalibration();
    return;  // hold the estimate still while calibrating
  }

  const float gxc = gx - g_bias[0], gyc = gy - g_bias[1], gzc = gz - g_bias[2];

  // Tilt: complementary filter (gyro for the short term, accelerometer for the long term).
  const float rollAcc = atan2f(ay, az) * 180.0f / (float)M_PI;
  const float pitchAcc = atan2f(-ax, sqrtf(ay * ay + az * az)) * 180.0f / (float)M_PI;
  g_roll = 0.98f * (g_roll + gxc * dt) + 0.02f * rollAcc;
  g_pitch = 0.98f * (g_pitch + gyc * dt) + 0.02f * pitchAcc;
  const float tiltAcc = atan2f(sqrtf(ax * ax + ay * ay), az) * 180.0f / (float)M_PI;
  g_tiltLp += 0.1f * (tiltAcc - g_tiltLp);  // low-passed: a bump is not a rollover

  // Yaw: clockwise-positive compass heading, so gyro-Z (counter-clockwise positive) is negated.
  const float rateCw = -gzc;
  g_rateLp += 0.3f * (rateCw - g_rateLp);
  g_yaw = (float)wrap360(g_yaw + rateCw * dt);
  g_noise += 0.02f * (fabsf(rateCw - g_rateLp) - g_noise);  // running gyro noise estimate

  g_imu.ok = true;
  g_imu.yawDeg = g_yaw;
  g_imu.yawRateDps = g_rateLp;
  g_imu.pitchDeg = g_pitch;
  g_imu.rollDeg = g_roll;
  g_imu.tiltDeg = g_tiltLp;
  g_imu.gyroNoiseDps = g_noise;
}

void imuFeedGpsCourse(float courseDeg, float speedMps) {
  if (!g_imu.ok || g_cal) return;
  if (speedMps < IMU_GPS_ALIGN_MIN_SPEED) return;
  if (fabsf(g_rateLp) > 15.0f) return;  // course over ground is meaningless while turning hard
  if (!g_imu.yawValid) {
    g_yaw = (float)wrap360(courseDeg);
    g_imu.yawDeg = g_yaw;
    g_imu.yawValid = true;
    serialLog("INFO", "heading aligned to the GPS course");
    return;
  }
  const double err = wrap180(courseDeg - g_yaw);
  if (fabs(err) > 60.0) {
    // One wild course is a GPS glitch. Several in a row mean the gyro heading is the thing that is wrong.
    if (++g_alignRejects >= 6) {
      g_yaw = (float)wrap360(courseDeg);
      g_alignRejects = 0;
    }
    return;
  }
  g_alignRejects = 0;
  const float k = constrain(0.08f * speedMps, 0.03f, 0.2f);
  g_yaw = (float)wrap360(g_yaw + k * err);
  g_imu.yawDeg = g_yaw;
}
#endif
