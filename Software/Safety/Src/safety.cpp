#include "safety.h"

namespace {
// Wrap-safe "a happened more than `ms` ago".
inline bool olderThan(uint32_t now, uint32_t then, uint32_t ms) { return (uint32_t)(now - then) > ms; }
}  // namespace

SafetyOutput safetyEvaluate(const SafetyInput& in, const SafetyParams& p, SafetyMemory* mem) {
  SafetyOutput out;

  // --- Debounced conditions are tracked in every mode so they are warm when arming. ---
  const bool tiltNow = in.imuOk && in.tiltDeg > p.tiltLimitDeg;
  if (tiltNow) {
    if (!mem->tilted) {
      mem->tilted = true;
      mem->tiltSinceMs = in.nowMs;
    }
  } else {
    mem->tilted = false;
  }

  const bool battNow = in.battKnown && in.battV < p.battCritV;
  if (battNow) {
    if (!mem->battLow) {
      mem->battLow = true;
      mem->battLowSinceMs = in.nowMs;
    }
  } else {
    mem->battLow = false;
  }

  const bool gpsGoodNow = in.gpsFix && in.gpsAgeMs <= 1500 && in.hdop <= p.hdopMax;
  if (!gpsGoodNow) {
    if (!mem->gpsBad) {
      mem->gpsBad = true;
      mem->gpsBadSinceMs = in.nowMs;
    }
  } else {
    mem->gpsBad = false;
  }

  if (in.mode == Mode::DISARMED) {
    out.reason = "";
    return out;  // allowMotion=false, disarm=false
  }

  // --- Rules that apply to every armed mode (these latch by disarming). ---
  if (mem->tilted && olderThan(in.nowMs, mem->tiltSinceMs, p.tiltDebounceMs)) {
    out.disarm = true;
    out.reason = "TILT";
    return out;
  }
  if (mem->battLow && olderThan(in.nowMs, mem->battLowSinceMs, p.battDebounceMs)) {
    out.disarm = true;
    out.reason = "BATT_CRIT";
    return out;
  }

  if (in.mode == Mode::MANUAL) {
    if (olderThan(in.nowMs, in.lastCmdMs, p.cmdTimeoutMs)) {
      out.allowMotion = false;  // stop, but stay armed so driving resumes with the next command
      out.reason = "CMD_TIMEOUT";
      return out;
    }
    out.allowMotion = true;
    return out;
  }

  // --- AUTO ---
  if (olderThan(in.nowMs, in.lastHbMs, p.linkTimeoutMs)) {
    out.disarm = true;
    out.reason = "LINK_LOST";
    return out;
  }
  if (in.follow == FollowState::FINISHED) {
    out.disarm = true;
    out.reason = "PATH_DONE";
    return out;
  }
  if (in.follow != FollowState::RUNNING) {
    out.disarm = true;
    out.reason = in.follow == FollowState::IDLE ? "NO_PATH" : followStateName(in.follow);
    return out;
  }
  if (!in.imuOk || !in.yawValid) {
    out.disarm = true;
    out.reason = "NO_HEADING";
    return out;
  }
  if (mem->gpsBad) {
    if (olderThan(in.nowMs, mem->gpsBadSinceMs, p.gpsGraceMs)) {
      out.disarm = true;
      out.reason = "NO_FIX";
      return out;
    }
    out.allowMotion = false;  // inside the grace window: hold still, do not give up yet
    out.reason = "GPS_HOLD";
    return out;
  }
  out.allowMotion = true;
  return out;
}

bool safetyCanArm(const SafetyInput& in, const SafetyParams& p, const char** why) {
  const char* dummy;
  if (!why) why = &dummy;
  if (in.imuOk && in.tiltDeg > 25.0f) {
    *why = "rover is tilted";
    return false;
  }
  if (in.battKnown && in.battV < p.battCritV + 0.3f) {
    *why = "battery too low";
    return false;
  }
  *why = "";
  return true;
}

bool safetyCanStartAuto(const SafetyInput& in, const SafetyParams& p, const char** why) {
  const char* dummy;
  if (!why) why = &dummy;
  if (!safetyCanArm(in, p, why)) return false;
  if (!(in.gpsFix && in.gpsAgeMs <= 1500)) {
    *why = "no GPS fix";
    return false;
  }
  if (in.hdop > p.hdopMax) {
    *why = "GPS accuracy too low (HDOP)";
    return false;
  }
  if (!in.imuOk) {
    *why = "IMU not responding";
    return false;
  }
  if (!in.yawValid) {
    *why = "heading not aligned: drive forward ~5 m in manual first";
    return false;
  }
  *why = "";
  return true;
}

SafetyParams stateSafetyParams(const RuntimeConfig& c) {
  SafetyParams p;
  p.cmdTimeoutMs = c.cmdTimeoutMs;
  p.linkTimeoutMs = c.linkTimeoutMs;
  p.hdopMax = c.hdopMax;
  p.tiltLimitDeg = IMU_TILT_LIMIT_DEG;
  p.battCritV = c.battCritV;
  return p;
}

SafetyInput stateSafetyInput(const RoverState& s, uint32_t nowMs) {
  SafetyInput in;
  in.mode = s.mode;
  in.nowMs = nowMs;
  in.lastCmdMs = s.lastCmdMs;
  in.lastHbMs = s.lastHbMs;
  in.gpsFix = s.gps.fix;
  in.gpsAgeMs = s.gps.fixAgeMs;
  in.hdop = s.gps.hdop;
  in.imuOk = s.imu.ok;
  in.yawValid = s.imu.yawValid;
  in.tiltDeg = s.imu.tiltDeg;
  in.battKnown = s.battKnown;
  in.battV = s.battV;
  in.follow = s.follow.state;
  return in;
}

#ifdef ARDUINO
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <string.h>
#include <sys/time.h>
#include "github_log.h"
#include "motors.h"

RoverState g_state;
static SemaphoreHandle_t g_mux = nullptr;

void stateBegin() {
  if (!g_mux) g_mux = xSemaphoreCreateMutex();
  g_state.cfg = RuntimeConfig();
  g_state.modeSinceMs = millis();
}

StateLock::StateLock() {
  if (g_mux) xSemaphoreTake(g_mux, portMAX_DELAY);
}
StateLock::~StateLock() {
  if (g_mux) xSemaphoreGive(g_mux);
}

RoverState stateSnapshot() {
  StateLock l;
  return g_state;
}



static void setMode(Mode m, const char* reason) {
  g_state.mode = m;
  g_state.modeSinceMs = millis();
  strncpy(g_state.stopReason, reason ? reason : "", sizeof(g_state.stopReason) - 1);
  g_state.stopReason[sizeof(g_state.stopReason) - 1] = '\0';
  g_state.cmdThrottle = g_state.cmdTurn = 0;
  g_state.benchWheel = -1;
}

bool stateArm(const char** why) {
  StateLock l;
  const char* dummy;
  if (!why) why = &dummy;
  if (g_state.mode != Mode::DISARMED) {
    *why = "already armed";
    return false;
  }
  const SafetyInput in = stateSafetyInput(g_state, millis());
  if (!safetyCanArm(in, stateSafetyParams(g_state.cfg), why)) return false;
  setMode(Mode::MANUAL, "");
  g_state.lastCmdMs = 0;  // no motion until the first fresh command arrives
  g_state.lastHbMs = millis();
  *why = "";
  return true;
}

void stateDisarm(const char* reason) {
  StateLock l;
  if (g_state.mode == Mode::DISARMED) {
    if (reason && *reason) strncpy(g_state.stopReason, reason, sizeof(g_state.stopReason) - 1);
    return;
  }
  setMode(Mode::DISARMED, reason);
}

bool stateStartAuto(const char** why) {
  StateLock l;
  const char* dummy;
  if (!why) why = &dummy;
  const SafetyInput in = stateSafetyInput(g_state, millis());
  if (!safetyCanStartAuto(in, stateSafetyParams(g_state.cfg), why)) return false;
  setMode(Mode::AUTO, "");
  g_state.lastHbMs = millis();
  *why = "";
  return true;
}

void stateDrive(float throttle, float turn) {
  StateLock l;
  if (!isfinite(throttle) || !isfinite(turn)) {  // garbage from the network means "stop"
    throttle = turn = 0;
  }
  g_state.cmdThrottle = constrain(throttle, -1.0f, 1.0f);
  g_state.cmdTurn = constrain(turn, -1.0f, 1.0f);
  g_state.lastCmdMs = millis();
  g_state.lastHbMs = g_state.lastCmdMs;
}

void stateHeartbeat() {
  StateLock l;
  g_state.lastHbMs = millis();
}

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
  ImuWindow imuWin;            // raw IMU readings averaged between two log rows
  StraightMonitor straight;    // veer from a straight line while no turn is commanded
  bool logWasOn = false;
  uint32_t fixesSinceRow = 0, lastRowMs = 0, lastGgaMs = 0;

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
    if (newFix) lastGgaMs = now;
    imuWin.add(im);

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

    // ---- 6. Straight-line check and the log sample ----
    // The commands that were actually applied: a turn the operator or the follower asked for is not "drift".
    float appliedT = 0, appliedS = 0;
    if (!so.disarm && so.allowMotion) {
      if (s.mode == Mode::MANUAL) {
        appliedT = s.cmdThrottle;
        appliedS = s.cmdTurn;
      } else if (s.mode == Mode::AUTO) {
        appliedT = fc.throttle;
        appliedS = fc.turn;
      }
    }
    straight.update(dt, im.yawRateDps, appliedT, appliedS, im.ok);

    // One row per GPS fix (or per Nth fix); if the GPS goes silent, one row a second so the gap shows in the data.
    const bool logOn = s.cfg.loggingEnabled && (s.mode != Mode::DISARMED || s.cfg.logAlways);
    if (logOn) {
      if (!logWasOn) {  // logging just started: begin a fresh averaging window
        imuWin.take();
        fixesSinceRow = 0;
        lastRowMs = now;
      }
      bool due = false;
      if (newFix) due = ++fixesSinceRow >= s.cfg.logEvery;
      else if (now - lastGgaMs >= LOG_HEARTBEAT_MS && now - lastRowMs >= LOG_HEARTBEAT_MS) due = true;
      if (due) {
        LogSample x;
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        x.sec = tv.tv_sec;
        x.usec = (int32_t)tv.tv_usec;
        x.uptimeMs = now;
        x.gps = gp;
        x.imu = im;
        x.win = imuWin.take();
        x.driftValid = straight.valid();
        x.driftDeg = straight.driftDeg();
        x.straightS = straight.straightS();
        x.mode = s.mode == Mode::AUTO ? LogMode::AUTO : (s.mode == Mode::MANUAL ? (s.recording ? LogMode::REC : LogMode::MAN) : LogMode::OFF);
        x.throttle = appliedT;
        x.turn = appliedS;
        x.battKnown = battKnown;
        x.battV = battV;
        x.xteM = fs.xteM;
        x.wp = fs.wpIndex;
        logPush(x);
        fixesSinceRow = 0;
        lastRowMs = now;
      }
    } else {
      imuWin.take();  // nothing is being logged: do not let stale sums build up
    }
    logWasOn = logOn;

    {
      StateLock l;
      g_state.driftValid = straight.valid();
      g_state.driftDeg = straight.driftDeg();
      g_state.straightS = straight.straightS();
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
#endif
