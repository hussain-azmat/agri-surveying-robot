GPS  -  position, speed and time (u-blox NEO-6M on a UART)
==========================================================

WHAT IT DOES
  Brings the NEO-6M up by itself (finds its baud rate, switches it to 38400 baud and 5 Hz, keeps only the GGA and RMC
  sentences; nothing is saved inside the module), then parses the NMEA stream byte by byte with checksum checks.
  Gives the rest of the firmware: lat / lon, fix quality, satellites, HDOP, speed, course over ground, UTC time
  (used to set the clock in the field so log files get real dates). A module that is unplugged and replugged is
  found again by itself. Implausible positions and bad checksums are rejected.

FOLDER
  Src/gps.h, gps.cpp   the module (NmeaParser + UBX configuration are plain C++; the UART driver is Arduino-only)
  Src/gps.ino          the GPS bench sketch: runs ONLY the GPS and prints it on the serial port
  Bin/gps.bin          the bench sketch, compiled (flash instructions in Software/MPU/Readme.txt, same commands)
  Test/test_nmea.cpp   laptop unit tests: real reference sentences, bad checksums, garbage, partial lines, UBX frames

HARDWARE
  GPS TX -> ESP32 GPIO 34 (PIN_GPS_RX, input-only, fine for receive); GPS RX <- GPIO 17 (PIN_GPS_TX, needed to
  configure the module). 3.3 V logic. The antenna is a ceramic patch on a u.FL cable. Mount it OUTSIDE the steel
  chassis, patch facing the sky, ideally on a metal ground plane, cable away from the motor leads and buck converters.

BUILD AND FLASH THE BENCH
  python tools/build_firmware.py gps --out build/bench/gps     (or open build/stage/gps/gps.ino in the Arduino IDE)
  Take the GPS OUTSIDE with a clear sky. Serial monitor 115200.

PASS CRITERIA   (details in Test/ON_ROVER_TEST.txt)
  - gps_link: healthy=true and baud=38400 within about 10 s of power-up.
  - rate_hz reads about 5.0.
  - A fix (fix=true) within a few minutes outdoors; hdop under 3; sats 6 or more.
  - lat / lon match where you are standing (check on a map); walking 20 m changes them by about 20 m.

RUN THE LAPTOP TESTS
  bash tools/test_host.sh            (or tools\test_host.ps1 on Windows; needs g++)
