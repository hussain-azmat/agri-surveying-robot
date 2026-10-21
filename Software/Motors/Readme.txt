Motors  -  four wheel drivers (one L298N module per wheel)
==========================================================

WHAT IT DOES
  Skid-steer drive: a throttle and a turn command become left / right wheel speeds (the mixer scales both sides down
  together when the sum would exceed full scale, so the turn radius is kept). Each wheel has IN1 / IN2 for direction
  and PWM on EN for speed; speeds change through a slew-rate limiter, EN is dropped to zero BEFORE any direction change,
  and an immediate stop sets targets and outputs to zero this instant (no ramp). NaN or infinite commands mean "stop".
  motorsBegin() is the first thing setup() calls: every EN and IN pin is forced LOW before anything else can run.

FOLDER
  Src/motors.h, motors.cpp   the module (the mixer in motors.h is plain C++; the pin driver is Arduino-only)
  Src/motors.ino             the Motors bench sketch: spin one wheel for 1 s per key press, nothing else
  Bin/motors.bin             the bench sketch, compiled (flash instructions in Software/MPU/Readme.txt, same commands)
  Test/test_motors.cpp       laptop unit tests of the mixer

HARDWARE   (pins in Software/Common/Src/config.h, wiring notes in docs/PINOUT.md)
                FL    FR    RL    RR
       IN1      25    27    32    18
       IN2      26    14    33    19
       EN(PWM)  13    23     4    16     <- RR moved from GPIO 21 (that is the IMU's I2C SDA)
  Remove the L298N EN jumpers so the ESP32 pin controls EN, and fit 10 k pull-downs from each EN pin to GND so the
  drivers stay off while the chip boots. If a wheel turns the wrong way, set it in WHEEL_INVERT (config.h) or swap
  that motor's two wires.

BUILD AND FLASH THE BENCH
  python tools/build_firmware.py motors --out build/bench/motors   (or open build/stage/motors/motors.ino in the IDE)
  WHEELS OFF THE GROUND. Serial monitor 115200: key 1-4 = FL FR RL RR forward for 1 s at 40%, 5-8 = reverse, s = stop.

PASS CRITERIA   (details in Test/ON_ROVER_TEST.txt)
  - Each key turns exactly the wheel it names, forward for 1-4 and backward for 5-8, for about 1 s.
  - Nothing twitches when you press reset 10 times with the monitor open (the pull-downs work).
  - Find the lowest PWM at which each wheel really turns and compare it with MOTOR_MIN_DUTY (0.18); adjust in config.h.

RUN THE LAPTOP TESTS
  bash tools/test_host.sh            (or tools\test_host.ps1 on Windows; needs g++)
