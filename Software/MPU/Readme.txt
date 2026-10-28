MPU  -  the inertial sensor (MPU6050 on I2C)
===========================================

WHAT IT DOES
  Reads the MPU6050 directly over I2C (no Adafruit library), removes the gyro's zero offset (calibration), works out
  pitch / roll / tilt, and keeps a compass-style heading (yaw). There is no magnetometer, so yaw comes from the gyro
  and is lined up with the GPS course the first time the rover drives straight; later GPS courses keep it from drifting.
  Tilt feeds the rollover failsafe; yaw feeds the path follower.

FOLDER
  Src/imu.h, imu.cpp   the module (ImuData: raw accel m/s^2, raw gyro rad/s, pitch, roll, tilt, yaw, gyro noise)
  Src/mpu.ino          the MPU bench sketch: runs ONLY the IMU and prints it on the serial port
  Bin/mpu.bin          the bench sketch, compiled (flash instructions below)
  Test/                what to check on the real rover (ON_ROVER_TEST.txt). The chip itself cannot run on a laptop.

HARDWARE
  I2C SDA = GPIO 21, SCL = GPIO 22 (PIN_I2C_SDA / PIN_I2C_SCL in Software/Common/Src/config.h), address 0x68, 400 kHz.
  ** The pins are an assumption from the schematics: CONFIRM THEM on the real rover. The bench prints the pins it uses
  and tells you if the chip answers, answers only with SDA/SCL swapped, or is at another address. **
  Mount flat, chip +X toward the front, +Z up (otherwise change the axis mapping in imu.cpp).

BUILD AND FLASH THE BENCH
  python tools/build_firmware.py mpu --out build/bench/mpu       (needs arduino-cli, see README.md)
  Or open build/stage/mpu/mpu.ino (made by: python tools/stage_sketch.py mpu) in the Arduino IDE.
  Or flash Bin/mpu.bin (the rover must already have had the full firmware flashed once, for the partition table):
      python -m esptool --chip esp32 --port COM5 erase_region 0xe000 0x2000
      python -m esptool --chip esp32 --port COM5 write_flash 0x10000 Software/MPU/Bin/mpu.bin
  Serial monitor 115200 baud. To go back to the full firmware, flash ota/firmware.bin (the latest full firmware) the same way.

PASS CRITERIA   (details in Test/ON_ROVER_TEST.txt)
  - The pin message shows the pins you wired and "MPU6050 Initialized Successfully".
  - {"mpu":{"ok":true,"calibrated":true,...}} within about 5 s of power-up with the rover still.
  - Flat and still: accel_z about 9.8, accel_x / accel_y and all gyro values about 0, tilt under 2 degrees.
  - Tilting the rover changes pitch / roll / tilt in the right direction.
  - gyro_noise_dps stays well under 0.5 with motors off, and does not rise sharply when the motors run
    (that is the full-firmware check in docs/BRINGUP.md, Stage 0).

IN THE FULL FIRMWARE
  imu.cpp is called ONLY from the 100 Hz control task (one owner of the I2C bus). Its values appear in the serial JSON
  {"imu":{...}}, on the control page, and in the log CSV (yaw_deg, pitch_deg, roll_deg, the MEAN accel_* and gyro_* between
  two rows, gyro_peak, drift_deg, straight_s).
  imu.h also holds two plain-C++ helpers with laptop tests (Test/test_imu.cpp): ImuWindow averages the 100 Hz readings
  between log rows, and StraightMonitor measures how far the rover veers from a straight line while no turn is commanded
  (integrating the gyro yaw rate during a straight run; + = to the right). See Software/Logging/Readme.txt.
