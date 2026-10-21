Safety  -  the failsafes, the shared rover state and the 100 Hz control task
============================================================================

WHAT IT DOES
  Decides, every 10 ms, whether the rover is allowed to move. The rules are one pure function (safetyEvaluate) so every
  rule is unit-tested on a laptop. The 100 Hz control task (core 1, task watchdog 3 s) owns the sensors, the failsafes and
  the motors, and never touches flash or the network, so a slow upload or an OTA flash cannot delay a stop.
  Modes: DISARMED / MANUAL / AUTO. Every boot and every OTA leaves the rover DISARMED. STOP means disarm.
  Rules: manual command timeout 500 ms; in AUTO the page link must stay alive (3 s), GPS must have a good fix (short
  blips tolerated, then hold, then disarm), heading must be valid, the path follower must not report off-path / stuck;
  rollover (tilt) disarms after a short debounce; battery low / critical act ONLY when the battery sensor is fitted.
  Also here: RoverState (the shared state snapshot) and the arm / start preconditions.

FOLDER
  Src/safety.h, safety.cpp   the module
  Test/test_safety.cpp       laptop unit tests: disarmed never moves, every timeout, GPS loss, HDOP, rollover debounce, battery, millis() wraparound
  Bin/                       no stand-alone image: safety runs inside the full firmware (see Rover)

PASS CRITERIA
  - Laptop: the unit tests pass.
  - Rover (docs/BRINGUP.md, Stage 3): wheels off the ground, then on a safe patch: let the control page go quiet -> the
    motors stop within 0.5 s; cover the GPS antenna in AUTO -> hold, then disarm; tilt past the limit -> disarm; STOP always stops.
  - Known limit: no battery failsafe until the battery divider is fitted (BATT_SENSE_ENABLED in config.h).
