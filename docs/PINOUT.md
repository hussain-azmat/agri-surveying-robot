# Pin map (PROVISIONAL)

The EDD (V0.5, s6.2) says the final GPIO assignment is intentionally not fixed while the PCB is revised. These are
**safe defaults** for an ESP32-WROOM-32 DevKit (38 pin). They live in `firmware/agri_rover/config.h`; **edit that file
to match the real wiring**, rebuild, and update this table. The build fails if a motor pin is a flash pin, an
input-only pin, a boot-strapping pin, or used twice (compile-time checks in `config.h`).

| Function | GPIO | Notes |
|---|---|---|
| Motor FL, input A / B | 25 / 26 | one L298N channel per wheel |
| Motor FR, input A / B | 27 / 32 | |
| Motor RL, input A / B | 33 / 4 | |
| Motor RR, input A / B | 16 / 17 | |
| MPU6050 SDA / SCL | 21 / 22 | I2C, 400 kHz |
| GPS RX (ESP32 receives from GPS TX) | 19 | UART1, remapped |
| GPS TX (ESP32 sends to GPS RX) | 23 | UART1, remapped |
| Battery sense (ADC1) | 34 | input only; needs a divider (below) |
| WiFi-setup button (to GND) | 15 | internal pull-up; hold ~5 s at power-up |
| Status LED | 2 | on-board LED |

## Motor drivers (L298N, one module per wheel)

With the `EN` jumper fitted (always enabled), speed is PWM on one input and direction is *which* input:

| Command | IN1 (A) | IN2 (B) |
|---|---|---|
| forward | PWM | LOW |
| reverse | LOW | PWM |
| stop | LOW | LOW |

If a wheel turns the wrong way on the bench, set its flag in `WHEEL_INVERT` (right-hand wheels usually need it).

**Fit 10 k pull-down resistors from each of the 8 driver inputs to GND.** For the ~200 ms between power-on and the
first line of `setup()`, GPIO pins float; a pull-down keeps the motors off during that window and during any watchdog
reset. The firmware drives every input LOW as the very first thing it does.

Avoid for motor outputs (the build enforces this): GPIO 0, 2, 5, 12, 14, 15 (boot-time behaviour), 6-11 (flash), 34-39 (input only).

## Battery divider (not on the schematic yet)

For a 3S Li-ion pack (12.6 V full) use 33 k (top) and 10 k (bottom): ratio 4.3, about 2.9 V at the ADC when full.
Then set `BATT_SENSE_ENABLED 1` and trim `BATT_CAL_FACTOR` against a multimeter. Add a 100 nF capacitor from the ADC pin to GND.
For other packs recompute so full charge stays under ~3.0 V, and set `batt_low_v` / `batt_crit_v` in `config/rover.cfg`.

## GPS module

u-blox **NEO-6M** (`NEO-6M-0-001`) breakout with a small ceramic patch antenna on a u.FL cable (replaces the SMA
active antenna described in the EDD). Power it from 3.3 V or 5 V per the breakout; its TX output is 3.3 V logic.
The firmware switches it to 38400 baud, 5 Hz, GGA+RMC only at every boot; nothing is saved in the module.

Antenna placement matters more than anything else for GPS quality: mount it **outside the steel chassis**, patch
facing the sky, **on a metal ground plane if you can**, and keep the cable away from the buck converters and motor
leads. Judge it from the control page: satellites, HDOP, and "GPS rate" should read about 5.0 Hz.

## IMU

MPU6050 mounted flat, chip +X toward the front of the rover, +Z up. If it is mounted differently, change the axis
mapping in `imu.cpp`. Keep the I2C wires short and away from the motor leads.
