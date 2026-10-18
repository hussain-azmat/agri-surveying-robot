#include "control.h"

#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "gps.h"
#include "imu.h"
#include "motors.h"
#include "path.h"
#include "settings.h"
#include "state.h"

namespace {

SafetyMemory g_safeMem;
#if BATT_SENSE_ENABLED
float g_battFilt = 0;
bool g_battSeen = false;
uint32_t g_battMs = 0;
#endif

void readBattery(uint32_t now, bool* known, float* volts) {
#if BATT_SENSE_ENABLED
  if (now - g_battMs >= 100) {
    g_battMs = now;
    uint32_t mv = 0;
    for (int i = 0; i < 8; i++) mv += analogReadMilliVolts(PIN_BATT_ADC);
    const float v = (mv / 8.0f) / 1000.0f * BATT_DIVIDER_RATIO * BATT_CAL_FACTOR;
    g_battFilt = g_battSeen ? g_battFilt + 0.1f * (v - g_battFilt) : v;
    g_battSeen = true;
  }
  *known = g_battSeen;
  *volts = g_battFilt;
#else
  (void)now;
  *known = false;  // no divider fitted: unknown, and no battery failsafe (see config.h)
  *volts = 0;
#endif
}

void controlTask(void*) {
  esp_task_wdt_add(nullptr);
  TickType_t wake = xTaskGetTickCount();
  uint32_t prevUs = micros();
  FollowCmd fc;  // last follower command
  FollowStatus fs;

  for (;;) {
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(CONTROL_PERIOD_MS));
    const uint32_t nowUs = micros();
    float dt = (nowUs - prevUs) * 1e-6f;
    prevUs = nowUs;
    if (!(dt > 0.001f)) dt = 0.001f;
    if (dt > 0.1f) dt = 0.1f;
    const uint32_t now = millis();

    // ---- 1. Sensors ----
    const uint8_t ev = gpsUpdate();
    imuUpdate(dt);
    GpsData gp = gpsGet();
    if ((ev & NMEA_RMC) && gp.courseValid) imuFeedGpsCourse(gp.courseDeg, gp.speedMps);
    const ImuData im = imuGet();
    bool battKnown;
    float battV;
    readBattery(now, &battKnown, &battV);
    const bool newFix = (ev & NMEA_GGA) != 0;

    // ---- 2. Publish sensors, take a consistent snapshot of commands and mode ----
    RoverState s;
    {
      StateLock l;
      g_state.gps = gp;
      g_state.imu = im;
      g_state.battKnown = battKnown;
      g_state.battV = battV;
      s = g_state;
    }

    // ---- 3. Path following (AUTO only) ----
    if (s.mode == Mode::AUTO) {
      FollowCmd c;
      FollowStatus st;
      if (pathAutoStep(gp, newFix, im, dt, &c, &st)) {
        fc = c;
        fs = st;
      } else if (fs.state == FollowState::IDLE) {
        // The path lock was busy on the very first AUTO tick: that is "unknown", not "no path".
        fs.state = FollowState::RUNNING;
      }  // otherwise reuse the last command for this 10 ms tick
    } else {
      fc = FollowCmd();
      fs = FollowStatus();
    }
    if (newFix && s.recording) pathRecFeed(gp, im, s.cfg);

    // ---- 4. Safety: ONE function decides whether anything may move ----
    SafetyInput in = stateSafetyInput(s, now);
    in.follow = s.mode == Mode::AUTO ? fs.state : FollowState::IDLE;
    const SafetyOutput so = safetyEvaluate(in, stateSafetyParams(s.cfg), &g_safeMem);

    // ---- 5. Act ----
    if (so.disarm) {
      motorsStop();
      pathAutoStop();
      stateDisarm(so.reason);
    } else if (!so.allowMotion) {
      // Bench (wiring check): one wheel, briefly, only while DISARMED and only if it was just requested.
      const bool bench = s.mode == Mode::DISARMED && s.benchWheel >= 0 && s.benchWheel < W_COUNT && now < s.benchUntilMs;
      if (bench) {
        float t[W_COUNT] = {0, 0, 0, 0};
        t[s.benchWheel] = constrain(s.benchValue, -0.5f, 0.5f);
        motorsSetWheels(t);
      } else {
        motorsStop();
      }
    } else if (s.mode == Mode::MANUAL) {
      motorsDrive(s.cmdThrottle, s.cmdTurn, s.cfg.maxSpeed);
    } else if (s.mode == Mode::AUTO) {
      motorsDrive(fc.throttle, fc.turn, 1.0f);
    }
    motorsUpdate(dt);

    {
      StateLock l;
      g_state.follow = fs;
      motorsGetApplied(g_state.wheel);
      g_state.recPoints = (uint16_t)pathRecCount();
      if (g_state.benchWheel >= 0 && now >= g_state.benchUntilMs) g_state.benchWheel = -1;
    }
    esp_task_wdt_reset();
  }
}

}  // namespace

void controlStart() {
  esp_task_wdt_config_t wc = {};
  wc.timeout_ms = CONTROL_WDT_S * 1000;
  wc.idle_core_mask = 0;  // only OUR task is watched: a long, legitimate TLS call must not reboot the rover
  wc.trigger_panic = true;
  if (esp_task_wdt_reconfigure(&wc) != ESP_OK) esp_task_wdt_init(&wc);
  xTaskCreatePinnedToCore(controlTask, "control", 8192, nullptr, 3, nullptr, 1);
}
