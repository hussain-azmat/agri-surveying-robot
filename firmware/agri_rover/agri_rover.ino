// =============================================================================
// Agri surveying rover firmware: main sketch.
//
// setup() and loop() only wire the modules together. Each module has one job:
//   config.h        pins, limits, version        motors      4 drivers, PWM, skid-steer mixing
//   imu             MPU6050, bias, heading       gps         NEO-6M at 5 Hz, fix quality
//   wifi_manager    saved network + setup page   ota_github  version check, download, flash
//   github_log      5 Hz log, batched upload     path        record, thin, save, load, follow
//   remote + ui.h   web server and pages         safety      failsafes (pure, unit-tested)
//   (safety also runs the 100 Hz real-time control task and holds the shared rover state)
//
// Threads: the 100 Hz control task (core 1) owns sensors, failsafes and motors and never touches
// flash or the network. This loop() owns WiFi, the web server, flash and GitHub. A slow upload or
// an OTA flash therefore cannot delay a stop.
// =============================================================================
#include <LittleFS.h>

#include "config.h"
#include "github_log.h"
#include "gps.h"
#include "imu.h"
#include "motors.h"
#include "ota_github.h"
#include "path.h"
#include "remote.h"
#include "safety.h"
#include "wifi_manager.h"

static Mode g_prevMode = Mode::DISARMED;
static char g_prevReason[24] = "";
static uint32_t g_lastCfgSyncMs = 0;

// "Stopped long enough" gate for anything that uses the network heavily.
static bool idleFor(const RoverState& s, uint32_t ms) {
  return s.mode == Mode::DISARMED && (uint32_t)(millis() - s.modeSinceMs) >= ms;
}

void setup() {
  // FIRST: every motor driver input LOW, before anything else can fail or take time.
  motorsBegin();

  Serial.begin(115200);
  delay(50);
  serialLog("INFO", "agri rover firmware v" FW_VERSION " starting");

  stateBegin();
  serialLog("INFO", "boot: id %s, MAC %s", roverId().c_str(), roverMac().c_str());

  if (!LittleFS.begin(true /*format if corrupt*/, "/littlefs", 10, "spiffs")) {
    serialLog("WARN", "flash filesystem unavailable: logging and paths disabled");
  }
  {
    StateLock l;
    g_state.cfg = runtimeConfigLoad();
  }

  const bool forceSetup = setupButtonHeldAtBoot();  // hold ~5 s at power-up to re-enter WiFi setup

  imuBegin();
  gpsBegin();
  pathBegin();
  logBegin();
  otaBegin();
  wifiBegin(forceSetup);
  remoteBegin();
  controlStart();  // from here the failsafes are live

  String from;
  if (otaJustUpdated(&from)) {
    serialLog("INFO", "now running v%s (updated from v%s). The rover is DISARMED: arm it from the page.", FW_VERSION,
              from.c_str());
  }
  serialStatus("READY", "DISARMED", nullptr);
}

void loop() {
  wifiUpdate();
  remoteUpdate();

  const RoverState s = stateSnapshot();

  // Log a row at 5 Hz while armed; record state changes (e.g. a failsafe disarm) as events.
  logTick(s);
  serialTelemetry(s);  // the {"imu":...} and {"gps":...} lines (the old sketch's serial format)
  if (s.mode != g_prevMode || strcmp(s.stopReason, g_prevReason) != 0) {
    if (s.mode == Mode::DISARMED && g_prevMode != Mode::DISARMED) {
      char ev[40];
      snprintf(ev, sizeof(ev), "DISARM:%s", s.stopReason);
      logEvent(s, ev);
      logFlush();
      serialStatus("STOPPED", "DISARMED", s.stopReason);
    } else if (s.mode != g_prevMode) {
      logEvent(s, s.mode == Mode::AUTO ? "ARM:AUTO" : "ARM:MANUAL");
      serialStatus("RUNNING", s.mode == Mode::AUTO ? "AUTO" : "MANUAL", nullptr);
    }
    g_prevMode = s.mode;
    strncpy(g_prevReason, s.stopReason, sizeof(g_prevReason) - 1);
  }

  // Network chores run ONLY when the rover is stopped, never while it can move.
  const bool online = wifiOnline() && wifiTimeSynced();
  const bool stopped = idleFor(s, 3000);
  otaService(online, stopped);
  logService(online, idleFor(s, s.cfg.uploadIdleS * 1000UL));
  pathService(online, stopped);
  if (online && stopped && millis() - g_lastCfgSyncMs > s.cfg.syncIntervalS * 1000UL) {
    g_lastCfgSyncMs = millis();
    String msg;
    runtimeConfigSync(&msg);
  }

  if (!wifiTimeSynced()) gpsSyncSystemClock();  // in the field: real dates from the GPS

  // Status LED: off = disarmed, slow blink = manual, fast blink = auto.
  static uint32_t ledMs = 0;
  if (millis() - ledMs > 100) {
    ledMs = millis();
    const uint32_t period = s.mode == Mode::AUTO ? 2 : (s.mode == Mode::MANUAL ? 5 : 0);
    digitalWrite(PIN_STATUS_LED, period ? ((millis() / 100) % period) < 1 : LOW);
  }
  delay(2);  // let the WiFi/web stack run
}
