Rover  -  the complete firmware (the one that runs the rover)
=============================================================

WHAT IT DOES
  agri_rover.ino is deliberately thin: setup() and loop() only wire the other functions together. Every job is in its own
  folder under Software/ (MPU, GPS, Motors, WiFi, OTA, Logging, Path, Remote, Safety; shared settings in Common).

FOLDER
  Src/agri_rover.ino   the main sketch
  Src/partitions.csv   flash layout: two OTA slots of 1.5 MB, NVS, 960 KB for logs / paths / config. The Arduino core uses it
                       automatically when it sits next to the sketch.
  Bin/                 the released image is ota/firmware.bin (+ ota/version.txt) at the repo root: that is the file the
                       rovers download, and the file to flash by USB for a first install. It is written by the CI build.
  Test/                the whole-rover bring-up checklist is docs/BRINGUP.md

HOW THE FOLDERS BECOME ONE FIRMWARE
  The Arduino toolchain wants one folder per sketch, so tools/stage_sketch.py copies the sketch plus every
  Software/*/Src/*.h/.cpp into build/stage/agri_rover/ and that is what is compiled:
      python tools/build_firmware.py                      (arduino-cli; the image lands in build/out/)
      python tools/stage_sketch.py                         (just stage it, then open build/stage/agri_rover/agri_rover.ino in the Arduino IDE)
  Edit the files in Software/, never the staged copies.

FIRST FLASH, AND UPDATES
  See README.md ("First-time setup") and docs/CONTRIBUTOR_GUIDE.md. After the first USB flash every update is over the air:
  bump FW_VERSION (python tools/bump_version.py patch), push, CI builds and publishes, rovers update themselves.
