OTA  -  GitHub client and over-the-air updates
==============================================

WHAT IT DOES
  Lets the rover update itself from this repository, with no cable.
    ota/version.txt on GitHub lists the newest firmware:  version=, md5=, size=
    At power-up, as soon as the rover is online with a valid clock, it reads that file. If the version is NEWER (numerically:
    0.10.0 is newer than 0.9.0; equal or older is ignored) it downloads ota/firmware.bin straight into the inactive one of
    two OTA slots, checks the byte count and the MD5, and only then switches to it. Any failure keeps the old firmware.
    It never updates while the rover can move, and always boots DISARMED afterwards. With auto_update=on (default, in
    config/rover.cfg) it installs by itself; with off it only reports, and the control page's "Install update" button does it.
  The same module is the GitHub client used by logging and paths. TLS is always checked against the embedded GitHub root
  certificates (certs.h) with no insecure fallback. A public repo is read without a token; WRITING (logs, paths) needs a
  fine-grained token entered on the setup page and stored only in flash.
  Also here: the runtime configuration (config/rover.cfg) - tunables fetched from the repo, every value clamped to a safe
  range, so tuning needs no rebuild and a typo cannot make the rover unsafe.

FOLDER
  Src/ota_github.h, ota_github.cpp   the module (version / manifest / config parsing are plain C++; the client is Arduino-only)
  Src/certs.h                        the trusted GitHub root certificates (regenerate with tools/gen_certs.py)
  Src/ota.ino                        the OTA bench: WiFi + update check/install, nothing else
  Bin/ota.bin                        the bench sketch, compiled (flash instructions in Software/MPU/Readme.txt)
  Test/test_ota.cpp                  laptop unit tests: versions, manifest, config clamping

RELEASING   (docs/RELEASING.md)
  python tools/bump_version.py patch ; git commit ; git push   -> GitHub Actions compiles it, writes ota/firmware.bin and
  ota/version.txt (version, MD5, size). There is no automatic rollback: test on one rover first.

BUILD AND FLASH THE BENCH
  python tools/build_firmware.py ota --out build/bench/ota       (or open build/stage/ota/ota.ino in the IDE)
  Serial monitor 115200: it checks once when online; type c = check again, i = install a newer version.

PASS CRITERIA   (details in Test/ON_ROVER_TEST.txt)
  - Same version in the repo: "up to date". Newer in the repo: "available", and i installs it and reboots into it.
  - A wrong MD5 / truncated download is reported and the old firmware keeps running.
  - Never installs while armed.
