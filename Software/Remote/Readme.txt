Remote  -  the web server and the control page
==============================================

WHAT IT DOES
  Drives the rover from a phone or laptop browser. The control page is laid out like a remote: a D-pad with eight directions
  (forward / back / left / right and the four diagonals) that you hold to drive, a centre STOP, a joystick alternative, a
  speed-limit slider (Slow / Medium / Fast), and the four wheel outputs. Keyboard: arrow keys or W A S D drive while held,
  Space or Esc = STOP. Beside it: ARM / DISARM, a compass with the heading, GPS / tilt / link / battery / WiFi-signal readouts,
  a live track map, record / load / follow paths with a progress bar, firmware update with progress, IMU calibration, a wheel
  test, system information (uptime, free memory, log queue), an activity log, and a light / dark theme. There is a small JSON
  API under /api/ behind it. On a shared network the page asks for the access code;
  clients of the rover's own WiFi are trusted through the WPA2 password. The page keeps sending a heartbeat; if it goes quiet
  the failsafe (Safety) stops the rover.

FOLDER
  Src/remote.h, remote.cpp   the web server and the API endpoints
  Src/ui.h                   the control page and the WiFi setup page (HTML / JavaScript inside the firmware)
  Test/                      the page is checked by tools/test_tools.py (self-contained, valid JavaScript, every control exists);
                             the real-rover checks are in Test/ON_ROVER_TEST.txt
  Bin/                       no stand-alone image: the server is part of the full firmware; the WiFi bench and the OTA
                             bench include it so the setup page works

TRY THE PAGE WITHOUT A ROVER
  python tools/ui_preview.py        then open http://localhost:8765
  A simulated rover answers the same /api/ as the firmware, so you can arm, drive (the map follows), record and load paths,
  and so on. It re-reads Software/Remote/Src/ui.h on every reload, so edit, save, reload.

PASS CRITERIA   (details in Test/ON_ROVER_TEST.txt)
  - The page loads from the rover's own network and from the shared network (mDNS rover-xxxxxx.local or the IP).
  - Arm, drive with the D-pad / arrow keys / joystick, release -> it stops; the big STOP button disarms.
  - Closing the page or walking out of WiFi range stops the rover within 0.5 s.
