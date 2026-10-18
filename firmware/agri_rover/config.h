#pragma once
// =============================================================================
// config.h - pins, limits and the firmware version.  NO SECRETS IN THIS FILE.
//
// WiFi passwords and the GitHub token live only in the ESP32's flash (NVS) and are
// entered through the setup page. This file is safe to commit.
//
// This header is deliberately free of Arduino includes so the pure-logic modules
// (nmea, geo, follow, safety, ...) can be unit-tested on a laptop.
// =============================================================================
#include <stddef.h>
#include <stdint.h>

// ---- Firmware version ------------------------------------------------------
// Bump this to release. CI (.github/workflows/build-firmware.yml) compiles the
// sketch and publishes ota/version.txt + ota/firmware.bin when it is NEWER than
// the version currently in ota/version.txt. Format: MAJOR.MINOR.PATCH
#define FW_VERSION "0.1.1"

// ---- GitHub repo used for OTA, config, paths and logs (non-secret) ---------
// Can be overridden at runtime from the setup page.
#define GH_OWNER_DEFAULT "hussain-azmat"
#define GH_REPO_DEFAULT "agri-surveying-robot"
#define GH_BRANCH "main"
#define GH_OTA_VERSION_PATH "ota/version.txt"
#define GH_OTA_FIRMWARE_PATH "ota/firmware.bin"
#define GH_CONFIG_PATH "config/rover.cfg"
#define GH_PATHS_DIR "paths"
#define GH_LOGS_DIR "logs"

// ---- Pins: PROVISIONAL --------------------------------------------------------
// The EDD (V0.5, s6.2) says final GPIO assignments are not fixed while the PCB is
// revised. These are safe defaults for an ESP32-WROOM-32 DevKit (38 pin):
//  * none of them are flash pins (6-11) or input-only pins (34-39) for outputs,
//  * none are boot-strapping pins that glitch at reset (0, 2, 5, 12, 14, 15) for
//    motor outputs, so the motors stay off while the chip boots.
// CHANGE THEM TO MATCH THE REAL WIRING, then rebuild. See docs/PINOUT.md.
//
// Wheel order everywhere: 0 = front-left, 1 = front-right, 2 = rear-left, 3 = rear-right
enum Wheel : uint8_t { W_FL = 0, W_FR = 1, W_RL = 2, W_RR = 3, W_COUNT = 4 };

// Each wheel has its own driver channel with two inputs (L298N IN1/IN2, EN jumpered HIGH):
//   forward: A = PWM, B = LOW      reverse: A = LOW, B = PWM      stop: both LOW
constexpr int PIN_MOTOR_A[W_COUNT] = {25, 27, 33, 16};
constexpr int PIN_MOTOR_B[W_COUNT] = {26, 32, 4, 17};
// Flip a wheel's direction here if it spins backwards on the bench (right side usually).
constexpr bool WHEEL_INVERT[W_COUNT] = {false, false, false, false};
// Per-wheel speed trim (1.0 = nominal). Use to even out motors that differ slightly.
constexpr float WHEEL_TRIM[W_COUNT] = {1.0f, 1.0f, 1.0f, 1.0f};

constexpr int PIN_I2C_SDA = 21;      // MPU6050
constexpr int PIN_I2C_SCL = 22;
constexpr int PIN_GPS_RX = 19;       // ESP32 RX  <- GPS TX
constexpr int PIN_GPS_TX = 23;       // ESP32 TX  -> GPS RX
constexpr int PIN_BATT_ADC = 34;     // ADC1 (input only): battery through a voltage divider
constexpr int PIN_SETUP_BTN = 15;    // to GND; hold ~5 s at boot to re-enter WiFi setup
constexpr int PIN_STATUS_LED = 2;    // on-board LED on most DevKits

// ---- Compile-time wiring sanity checks (a wrong pin map fails the build, not the field test)
constexpr bool pinCanOutput(int p) { return p >= 0 && p <= 33 && !(p >= 6 && p <= 11); }
constexpr bool pinSafeAtBoot(int p) { return !(p == 0 || p == 2 || p == 5 || p == 12 || p == 14 || p == 15); }
constexpr bool motorPinsOk() {
  for (int i = 0; i < W_COUNT; i++) {
    if (!pinCanOutput(PIN_MOTOR_A[i]) || !pinCanOutput(PIN_MOTOR_B[i])) return false;
    if (!pinSafeAtBoot(PIN_MOTOR_A[i]) || !pinSafeAtBoot(PIN_MOTOR_B[i])) return false;
    for (int j = 0; j < W_COUNT; j++) {
      if (j != i && (PIN_MOTOR_A[i] == PIN_MOTOR_A[j] || PIN_MOTOR_A[i] == PIN_MOTOR_B[j])) return false;
      if (j != i && (PIN_MOTOR_B[i] == PIN_MOTOR_B[j])) return false;
    }
    if (PIN_MOTOR_A[i] == PIN_MOTOR_B[i]) return false;
  }
  return true;
}
static_assert(motorPinsOk(), "Motor pin map is invalid: duplicates, flash pins, input-only pins or boot-strap pins");

// ---- Motors ---------------------------------------------------------------------
constexpr uint32_t MOTOR_PWM_HZ = 10000;
constexpr uint8_t MOTOR_PWM_BITS = 10;
constexpr float MOTOR_MIN_DUTY = 0.18f;    // below this the GA25-370 will not turn; BENCH-CALIBRATE
constexpr float MOTOR_RAMP_PER_S = 2.5f;   // slew limit (1.0 = full scale); stop is always immediate

// ---- Control loop ---------------------------------------------------------------
constexpr uint32_t CONTROL_PERIOD_MS = 10;       // 100 Hz: IMU, GPS drain, safety, motors
constexpr uint32_t CONTROL_WDT_S = 3;            // task watchdog: reset the chip if the loop hangs

// ---- IMU (MPU6050) --------------------------------------------------------------
constexpr uint8_t IMU_I2C_ADDR = 0x68;
constexpr uint32_t IMU_I2C_HZ = 400000;
constexpr float IMU_TILT_LIMIT_DEG = 50.0f;      // beyond this the rover is considered rolled over
constexpr float IMU_GPS_ALIGN_MIN_SPEED = 0.5f;  // m/s: GPS course is trusted above this speed
// Axis map: sensor mounted flat, chip's +Z up, +X toward the front. If mounted otherwise, edit imu.cpp.

// ---- GPS (u-blox NEO-6M) --------------------------------------------------------
constexpr uint32_t GPS_BAUD_TARGET = 38400;      // 9600 cannot carry 5 Hz NMEA; raised at boot
constexpr uint32_t GPS_BAUD_DEFAULT = 9600;
constexpr uint16_t GPS_RATE_MS = 200;            // 5 Hz (the NEO-6M maximum)
constexpr uint32_t GPS_MAX_AGE_MS = 1500;        // a fix older than this is not a fix

// ---- Battery sense --------------------------------------------------------------
// The EDD notes there is NO battery divider on the schematic yet. Until one is fitted
// and BATT_SENSE_ENABLED is set to 1, battery voltage is reported as unknown and no
// battery failsafe is applied (reading a floating pin would trigger false stops).
#define BATT_SENSE_ENABLED 0
constexpr float BATT_DIVIDER_RATIO = 4.3f;       // (R1+R2)/R2, e.g. 33k over 10k
constexpr float BATT_CAL_FACTOR = 1.0f;          // trim after comparing with a multimeter

// ---- Runtime-tunable defaults (overridable from config/rover.cfg, with clamps) -------
constexpr float DEF_MAX_SPEED = 0.8f;            // manual throttle scale, 0..1
constexpr float DEF_AUTO_SPEED = 0.6f;
constexpr float DEF_ACCEPT_RADIUS_M = 4.0f;      // brief: >= 3-5 m (NEO-6M is ~2.5 m CEP)
constexpr float DEF_OFFPATH_M = 8.0f;
constexpr uint32_t DEF_CMD_TIMEOUT_MS = 500;     // brief: stop if commands stop for ~0.5 s
constexpr uint32_t DEF_LINK_TIMEOUT_MS = 3000;   // auto mode: heartbeat from the page
constexpr float DEF_HDOP_MAX = 5.0f;
constexpr float DEF_BATT_LOW_V = 10.2f;          // 3S Li-ion: 3.4 V/cell. Pack dependent!
constexpr float DEF_BATT_CRIT_V = 9.6f;          //            3.2 V/cell
constexpr float DEF_PATH_SPACING_M = 1.5f;       // record: a point every 1-2 m ...
constexpr float DEF_PATH_TURN_DEG = 15.0f;       // ... or on a noticeable turn
constexpr uint32_t DEF_UPLOAD_IDLE_S = 20;       // idle this long before uploading logs
constexpr uint32_t DEF_SYNC_INTERVAL_S = 60;     // config sync while idle (EDD: ~1 min)
constexpr uint32_t DEF_OTA_INTERVAL_S = 300;     // version check while stopped (EDD: ~5 min)

// ---- WiFi -----------------------------------------------------------------------
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;   // brief: 10-15 s, non-blocking
constexpr uint32_t WIFI_PORTAL_TIMEOUT_MS = 240000;   // brief: 3-5 min then go offline
constexpr uint32_t SETUP_BTN_HOLD_MS = 5000;          // brief: hold ~5 s at boot
// The setup/control AP password is derived from the chip's MAC and this salt, so it can be
// printed on a sticker (tools/ap_password.py). Change the salt per fleet if you like.
#define AP_PASSWORD_SALT "agri-rover-v1"

// ---- Logging --------------------------------------------------------------------
constexpr uint32_t LOG_PERIOD_MS = 200;               // 5 Hz
constexpr size_t LOG_UPLOAD_CHUNK_BYTES = 16384;      // one GitHub commit per chunk; RAM-safe
constexpr uint32_t LOG_FS_RESERVE_BYTES = 40960;      // stop logging when this little flash is left

// ---- Paths ------------------------------------------------------------------------
constexpr int MAX_PATH_POINTS = 2000;
constexpr double PATH_SIMPLIFY_EPS_M = 0.4;           // Douglas-Peucker tolerance on save
