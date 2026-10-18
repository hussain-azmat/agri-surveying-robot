# Pin map

**Source of truth:** the running sketch `surveying_robot.ino` (Oct 10 2026, repo `hussain-azmat/surveying_robot`) and
EDD V0.5: **four separate L298N modules, one per wheel.** Older schematics in that repo show other generations
(Aug 19: four BTS7960; Sep 8 "FINAL SCHEMA": two L298N modules, one per side) and should be treated as superseded.

The pins live in `firmware/agri_rover/config.h`. The build **fails** if two functions share a GPIO, if a motor EN pin is a
flash, input-only or boot-sensitive pin, or if an IN pin is one of the boot-strapping pins (tests prove it rejects the
sketch's original clash, see below).

| Function | GPIO | Notes |
|---|---|---|
| FL: IN1 / IN2 / EN | 25 / 26 / 13 | same as the sketch |
| FR: IN1 / IN2 / EN | 27 / 14 / 23 | same as the sketch |
| RL ("BL" in the sketch): IN1 / IN2 / EN | 32 / 33 / 4 | same as the sketch |
| RR ("BR" in the sketch): IN1 / IN2 / EN | 18 / 19 / **16** | **EN moved from 21, see below** |
| MPU6050 SDA / SCL | 21 / 22 | I2C, 400 kHz (every schematic puts it here) |
| GPS RX (ESP32 receives from GPS TX) | 34 | input-only pin, fine for a receive line |
| GPS TX (ESP32 sends to GPS RX) | 17 | needed to configure the NEO-6M |
| Battery sense (ADC1) | 35 | input-only; needs a divider (below) |
| WiFi-setup button (to GND) | 15 | internal pull-up; hold ~5 s at power-up |
| Status LED | 2 | on-board LED |

## Wiring changes needed relative to the current sketch

1. **Move RR's EN wire from GPIO 21 to GPIO 16.** In the sketch `BR_EN = 21`, but GPIO 21 is also the I2C data line:
   `Wire.begin()` uses 21/22 by default and every schematic wires the MPU6050 to them. `pinMode(21, OUTPUT)` after
   `Wire.begin()` takes the pin away from I2C, so the IMU would stop answering after setup (and the sketch's own comment, "GPIO 22 is
   free for MPU6050 SCL", missed that SDA is 21 too). If your wiring is actually different (for example the IMU is not on 21), tell me
   and change `PIN_I2C_SDA` / `PIN_MOTOR_EN` in `config.h`: the build will tell you if the result still clashes.
2. **GPS**: connect GPS TX to GPIO 34 and GPS RX to GPIO 17 (the sketch has no GPS yet).
3. **Battery divider** (optional, later): midpoint to GPIO 35.

## How each L298N module is driven

The sketch only switched EN on/off (full speed). The new firmware puts **PWM on EN** for speed control and uses IN1/IN2 for direction:

| Command | IN1 | IN2 | EN |
|---|---|---|---|
| forward | HIGH | LOW | PWM |
| reverse | LOW | HIGH | PWM |
| stop | LOW | LOW | 0 |

For this the L298N's **EN jumper must be removed** so the ESP32 pin is connected (the sketch already assumes that; the EDD's "EN jumpered
HIGH" note describes the earlier plan). EN is dropped to zero *before* any direction change, and cleared first on every stop.

If a wheel turns the wrong way on the bench, set its flag in `WHEEL_INVERT` (or swap that motor's two wires).

**Fit 10 k pull-down resistors from each of the 4 EN pins to GND.** With the jumper removed the EN inputs float, and EN is the gate
that keeps a driver off during the ~200 ms between power-on and the first line of `setup()`, and during any watchdog reset. (The IN pins
matter less: with EN low, nothing can move. GPIO 14, the FR IN2 line, emits a short PWM burst at boot for that reason.)

## Battery divider (not on the schematic yet)

For a 3S Li-ion pack (12.6 V full) use 33 k (top) and 10 k (bottom): ratio 4.3, about 2.9 V at the ADC when full. Then set
`BATT_SENSE_ENABLED 1` and trim `BATT_CAL_FACTOR` against a multimeter. Add a 100 nF capacitor from the ADC pin to GND.
For other packs recompute so full charge stays under ~3.0 V, and set `batt_low_v` / `batt_crit_v` in `config/rover.cfg`.

## GPS module

u-blox **NEO-6M** (`NEO-6M-0-001`) breakout with a small ceramic patch antenna on a u.FL cable (replaces the SMA active antenna
described in the EDD). Power it from 3.3 V or 5 V per the breakout; its TX output is 3.3 V logic. The firmware switches it to
38400 baud, 5 Hz, GGA+RMC only at every boot; nothing is saved in the module.

Antenna placement matters more than anything else for GPS quality: mount it **outside the steel chassis**, patch facing the sky,
**on a metal ground plane if you can**, and keep the cable away from the buck converters and motor leads. Judge it from the control
page: satellites, HDOP, and "GPS rate" should read about 5.0 Hz.

## IMU

MPU6050 mounted flat, chip +X toward the front of the rover, +Z up. If it is mounted differently, change the axis mapping in `imu.cpp`.
Keep the I2C wires short and away from the motor leads. (This firmware talks to the chip's registers directly instead of the Adafruit
library your sketch uses; both give the same data, and the first bench run will confirm it on your unit.)
