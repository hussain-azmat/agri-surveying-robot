Remote  -  the web server and the control page
==============================================

WHAT IT DOES
  Drives the rover from a phone or laptop browser. Serves the control page (joystick, big red STOP, arm / disarm,
  record a path, load a path, start AUTO, system buttons: check / install update, calibrate the IMU, spin one wheel for
  a wiring check, change WiFi) and a small JSON API under /api/. On a shared network the page asks for the access code;
  clients of the rover's own WiFi are trusted through the WPA2 password. The page keeps sending a heartbeat; if it goes quiet
  the failsafe (Safety) stops the rover.

FOLDER
  Src/remote.h, remote.cpp   the web server and the API endpoints
  Src/ui.h                   the control page and the WiFi setup page (HTML / JavaScript inside the firmware)
  Test/                      none yet: the endpoints are checked on the real rover (Test/ON_ROVER_TEST.txt)
  Bin/                       no stand-alone image: the server is part of the full firmware; the WiFi bench and the OTA
                             bench include it so the setup page works

PASS CRITERIA   (details in Test/ON_ROVER_TEST.txt)
  - The page loads from the rover's own network and from the shared network (mDNS rover-xxxxxx.local or the IP).
  - Arm, drive with the joystick, release -> it stops; the big STOP button disarms.
  - Closing the page or walking out of WiFi range stops the rover within 0.5 s.
