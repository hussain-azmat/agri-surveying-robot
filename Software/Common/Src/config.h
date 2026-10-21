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
#define FW_VERSION "0.3.0"

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

// ---- Pins ---------------------------------------------------------------------------------
// SOURCE OF TRUTH: the running sketch surveying_robot.ino (Oct 10 2026) in the hardware repo, which matches
// EDD V0.5: FOUR separate L298N modules, one per wheel (not the older paired-driver schematics).
// Each module is driven by three ESP32 pins: IN1 and IN2 choose the direction, EN carries the PWM speed.
//
// Two deliberate differences from that sketch (see docs/PINOUT.md, "Wiring changes needed"):
//  * BR_EN moves from GPIO 21 to GPIO 16: GPIO 21 is the I2C data line (SDA) of the MPU6050, so the
//    sketch's pinMode(21, OUTPUT) would break the IMU. (Every schematic puts I2C on 21/22.)
//  * The sketch drives EN only on/off; here EN gets PWM, so speed is controllable.
//
// Wheel order everywhere: 0 = front-left, 1 = front-right, 2 = rear-left, 3 = rear-right
enum Wheel : uint8_t { W_FL = 0, W_FR = 1, W_RL = 2, W_RR = 3, W_COUNT = 4 };

//                                        FL  FR  RL  RR
constexpr int PIN_MOTOR_IN1[W_COUNT] = {25, 27, 32, 18};
constexpr int PIN_MOTOR_IN2[W_COUNT] = {26, 14, 33, 19};
constexpr int PIN_MOTOR_EN[W_COUNT] = {13, 23, 4, 16};   // PWM speed. The enable gate: fit pull-downs here
// Flip a wheel's direction here if it spins backwards on the bench (or swap its motor wires).
constexpr bool WHEEL_INVERT[W_COUNT] = {false, false, false, false};
// Per-wheel speed trim (1.0 = nominal). Use to even out motors that differ slightly.
constexpr float WHEEL_TRIM[W_COUNT] = {1.0f, 1.0f, 1.0f, 1.0f};

// CONFIRM THE IMU PINS ON THE REAL ROVER. Every schematic and the block diagram put the MPU6050 on I2C GPIO 21 (SDA) and
// GPIO 22 (SCL), and the Oct-10 sketch used Wire.begin() (those same defaults). If your IMU is wired elsewhere, change
// the two numbers here: the build re-checks that nothing else uses them. At every boot the rover prints the pins it
// uses and, if the IMU does not answer, scans the bus (also with SDA/SCL swapped) and says what it found:
// {"status":"ERROR","message":"..."}. See docs/PINOUT.md, "Confirm the IMU pins".
constexpr int PIN_I2C_SDA = 21;      // MPU6050 data line
constexpr int PIN_I2C_SCL = 22;      // MPU6050 clock line
constexpr int PIN_GPS_RX = 34;       // ESP32 RX  <- GPS TX. GPIO34 is input-only, which is all a receive pin needs
constexpr int PIN_GPS_TX = 17;       // ESP32 TX  -> GPS RX (used to configure the NEO-6M)
constexpr int PIN_BATT_ADC = 35;     // ADC1 (input only): battery through a voltage divider
constexpr int PIN_SETUP_BTN = 15;    // to GND; hold ~5 s at boot to re-enter WiFi setup
constexpr int PIN_STATUS_LED = 2;    // on-board LED on most DevKits

// ---- Compile-time wiring sanity checks (a wrong pin map fails the build, not the field test) ----
constexpr bool pinCanOutput(int p) { return p >= 0 && p <= 33 && !(p >= 6 && p <= 11); }
// Boot-strapping or boot-active pins: 0 2 5 12 15 are sampled/driven at reset; 14 emits PWM at boot.
constexpr bool pinBootSensitive(int p) { return p == 0 || p == 2 || p == 5 || p == 12 || p == 14 || p == 15; }
// IN pins may be GPIO14 (PWM at boot) because EN is low then; the other boot pins are still out.
constexpr bool inPinOk(int p) { return pinCanOutput(p) && (!pinBootSensitive(p) || p == 14); }
constexpr bool motorPinsOk() {
  for (int i = 0; i < W_COUNT; i++) {
    // EN is the gate that keeps a driver off while the chip boots, so it must be a quiet pin.
    if (!pinCanOutput(PIN_MOTOR_EN[i]) || pinBootSensitive(PIN_MOTOR_EN[i])) return false;
    if (!inPinOk(PIN_MOTOR_IN1[i]) || !inPinOk(PIN_MOTOR_IN2[i])) return false;
  }
  return true;
}
constexpr bool allPinsUnique() {
  int all[W_COUNT * 3 + 7] = {};
  int n = 0;
  for (int i = 0; i < W_COUNT; i++) {
    all[n++] = PIN_MOTOR_IN1[i];
    all[n++] = PIN_MOTOR_IN2[i];
    all[n++] = PIN_MOTOR_EN[i];
  }
  all[n++] = PIN_I2C_SDA;
  all[n++] = PIN_I2C_SCL;
  all[n++] = PIN_GPS_RX;
  all[n++] = PIN_GPS_TX;
  all[n++] = PIN_BATT_ADC;
  all[n++] = PIN_SETUP_BTN;
  all[n++] = PIN_STATUS_LED;
  for (int i = 0; i < n; i++)
    for (int j = i + 1; j < n; j++)
      if (all[i] == all[j]) return false;
  return true;
}
static_assert(motorPinsOk(), "Motor pin map invalid: flash/input-only pin, or a boot-sensitive pin used for EN");
static_assert(allPinsUnique(), "Two functions share one GPIO (for example I2C SDA and a motor EN)");
static_assert(pinCanOutput(PIN_GPS_TX) && pinCanOutput(PIN_STATUS_LED), "GPS TX and the LED need output-capable pins");

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

// ---- Runtime configuration -------------------------------------------------------------------
// The values the rover can be re-tuned with WITHOUT a rebuild: config/rover.cfg in the repo is fetched
// about once a minute while the rover is stopped and online, parsed by parseRtConfig() (ota_github.cpp)
// and clamped to the safe range noted beside each field, so a typo can make the rover less capable but
// never unsafe. The compile-time defaults above are what you get if the file is absent.
struct RuntimeConfig {
  float maxSpeed = DEF_MAX_SPEED;           // 0.1 .. 1.0   manual throttle scale
  float autoSpeed = DEF_AUTO_SPEED;         // 0.1 .. 0.8
  float acceptRadiusM = DEF_ACCEPT_RADIUS_M;  // 3 .. 15
  float offPathM = DEF_OFFPATH_M;           // acceptRadius+2 .. 50
  uint32_t cmdTimeoutMs = DEF_CMD_TIMEOUT_MS;    // 200 .. 1000
  uint32_t linkTimeoutMs = DEF_LINK_TIMEOUT_MS;  // 1000 .. 10000
  float hdopMax = DEF_HDOP_MAX;             // 1 .. 20
  float battLowV = DEF_BATT_LOW_V;          // 6 .. 30
  float battCritV = DEF_BATT_CRIT_V;        // 5 .. battLow
  float pathSpacingM = DEF_PATH_SPACING_M;  // 1 .. 5
  float pathTurnDeg = DEF_PATH_TURN_DEG;    // 5 .. 45
  uint32_t uploadIdleS = DEF_UPLOAD_IDLE_S;      // 5 .. 600
  uint32_t syncIntervalS = DEF_SYNC_INTERVAL_S;  // 30 .. 3600
  uint32_t otaIntervalS = DEF_OTA_INTERVAL_S;    // 60 .. 86400
  float turnFullDps = 90.0f;                // 20 .. 360  yaw rate at turn = 1 (calibrate!)
  bool loggingEnabled = true;
  bool autoUpdate = true;  // install a newer firmware by itself when the rover is stopped and online
};

// ---- Serial output ----------------------------------------------------------------------------
// The serial port carries JSON lines only, one object per line, in the style of the original surveying_robot.ino:
//   {"imu":{"accel_x":..,"accel_y":..,"accel_z":..,"gyro_x":..,"gyro_y":..,"gyro_z":..}}   accel in m/s^2, gyro in rad/s
//   {"gps":{...}}   {"status":"INFO","message":"..."}   {"status":"RUNNING","state":"MANUAL"}   {"event":"OTA_START"}
// so a laptop logger that parses the old stream keeps working. Set SERIAL_TELEMETRY to 0 to silence the periodic
// imu/gps lines (status and event lines are always printed).
#define SERIAL_TELEMETRY 1
constexpr uint32_t SERIAL_IMU_PERIOD_MS = 500;   // same rate as the original sketch
constexpr uint32_t SERIAL_GPS_PERIOD_MS = 1000;

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
