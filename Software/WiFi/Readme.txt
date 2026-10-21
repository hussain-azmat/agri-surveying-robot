WiFi  -  join a network, setup page, offline mode
=================================================

WHAT IT DOES
  Keeps the rover reachable and online without ever blocking driving.
    boot -> saved network? -> CONNECTING (15 s, non-blocking) -> ONLINE
         -> none saved, or it failed -> SETUP: the rover's own network "RoverSetup-xxxx" with a page that lists nearby
            networks. You pick one and enter its password; the rover TESTS the connection and saves it only if it works.
         -> nobody finishes setup within 4 minutes -> OFFLINE: the rover's own network stays up, so driving from a phone
            still works; OTA and log uploads simply do not run.
  Re-enter setup: hold the setup button (GPIO 15 to GND) about 5 s at power-up, or "Change WiFi" on the control page.
  Also here: the rover's identity (id "rover-A1B2C3" and MAC from the chip) and the ACCESS CODE. The code is derived from
  the MAC plus a salt (AP_PASSWORD_SALT in config.h), so it can be printed on a sticker:
      python tools/ap_password.py <MAC>
  It is the password of the rover's own network and protects the control page on shared networks. HONEST LIMIT: anyone
  who knows the salt (it is in the source) and the MAC can compute it. It keeps casual users out; choose your own code
  on the setup page, and change the salt per deployment.
  Credentials (WiFi password, access code) live only in the chip's flash. They are never printed, logged or committed.

FOLDER
  Src/wifi_manager.h, wifi_manager.cpp   the module (the access-code derivation is plain C++, the radio code Arduino-only)
  Src/wifi.ino                           the WiFi bench: WiFi + setup page + web server, nothing else
  Bin/wifi.bin                           the bench sketch, compiled (flash instructions in Software/MPU/Readme.txt)
  Test/test_wifi.cpp                     laptop unit tests of the access code (it must match tools/ap_password.py)

BUILD AND FLASH THE BENCH
  python tools/build_firmware.py wifi --out build/bench/wifi     (or open build/stage/wifi/wifi.ino in the IDE)

PASS CRITERIA   (details in Test/ON_ROVER_TEST.txt)
  - With no saved WiFi, "RoverSetup-xxxx" appears; its password is the code from tools/ap_password.py <MAC>.
  - Joining it shows the setup page on any web address.
  - A WRONG password is reported as a failure and nothing is saved.
  - The RIGHT password reports "connected and saved"; after a reboot the rover reconnects by itself.
  - Setup button held 5 s at power-up brings the setup page back. 4 minutes of doing nothing in setup -> offline mode.
