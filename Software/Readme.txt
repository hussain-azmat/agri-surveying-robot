Software  -  one folder per function
====================================

Every function of the rover has its own folder with the same layout:

    <Function>/
        Readme.txt     what it does, the hardware, how to build / flash its bench, the pass criteria
        Src/           the code of that function (.h / .cpp) and, where it has hardware, its bench sketch (.ino)
        Bin/           the compiled bench sketch (flash it to test ONLY that function)
        Test/          laptop unit tests (test_*.cpp) and the on-rover checklist (ON_ROVER_TEST.txt)

    MPU/       inertial sensor MPU6050: pitch, roll, tilt, heading                 bench: mpu.ino   -> Bin/mpu.bin
    GPS/       u-blox NEO-6M: position, speed, course, time                        bench: gps.ino   -> Bin/gps.bin
    Motors/    four L298N wheel drivers, skid-steer mixer                          bench: motors.ino-> Bin/motors.bin
    WiFi/      join a network, setup page, offline mode, identity, access code     bench: wifi.ino  -> Bin/wifi.bin
    OTA/       GitHub client, over-the-air updates, runtime config                 bench: ota.ino   -> Bin/ota.bin
    Logging/   serial JSON lines, 5 Hz CSV log, batched upload to GitHub           (inside the full firmware)
    Path/      record a route, thin and save it, follow it                         (inside the full firmware)
    Remote/    web server, control page, the /api/ endpoints                       (inside the full firmware)
    Safety/    failsafes, shared rover state, 100 Hz control task                  (inside the full firmware)
    Common/    config.h (pins, version, limits) and the test harness
    Rover/     the complete firmware: agri_rover.ino (thin main) + partitions.csv; released as ota/firmware.bin

Why a "bench" per function: you can flash ONE function onto the rover and prove it works on its own (the MPU alone, the GPS
alone, ...) before everything is put together. Each bench prints JSON lines on the serial port (115200 baud).

How the folders become firmware
    The Arduino toolchain needs one folder per sketch, so tools/stage_sketch.py gathers a sketch plus every Software/*/Src
    file into build/stage/<name>/. tools/build_firmware.py does that and then compiles:
        python tools/build_firmware.py                       the full firmware       -> build/out/agri_rover.ino.bin
        python tools/build_firmware.py mpu --out build/bench/mpu       one bench
        python tools/build_benches.py                        every bench, and refresh each Software/<Function>/Bin/*.bin
    Edit the files in Software/, never the copies in build/.

Laptop tests (no ESP32 needed):   bash tools/test_host.sh     (Windows: tools\test_host.ps1)    needs g++
Repo-level docs: README.md, docs/ARCHITECTURE.md, docs/PINOUT.md, docs/BRINGUP.md, docs/RELEASING.md, docs/CONTRIBUTOR_GUIDE.md
