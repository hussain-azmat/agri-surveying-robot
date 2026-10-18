#pragma once
// u-blox NEO-6M on a UART: bring-up (auto-baud, 5 Hz, only GGA+RMC) and NMEA parsing.
// Called ONLY from the control task (one owner of the UART), apart from gpsBegin().
//
// Bring-up is non-blocking and self-healing. The module's power-on default is 9600 baud / 1 Hz,
// but a module that stayed powered (backup battery) may still be at 38400 from the last boot, so
// the driver listens at one speed and, after 3 s of silence, tries the other. When it hears valid
// NMEA at 9600 it configures the module (38400 baud, 200 ms rate, GGA+RMC only). A GPS that is
// unplugged and reconnected, or reset, is found again the same way. The receiver's own flash is
// never written.
#include <Arduino.h>

#include "nmea.h"
#include "state.h"

void gpsBegin();
// Drain the UART and parse. Returns a bitmask of NmeaEvent for sentences completed in this call.
uint8_t gpsUpdate();
// Snapshot of the latest data (valid fix means: a position, fresh within GPS_MAX_AGE_MS).
GpsData gpsGet();
// Sets the ESP32 system clock from GPS UTC when WiFi/NTP has not (so log rows and file names get
// real dates in the field). Safe to call from any task; does nothing if already set.
void gpsSyncSystemClock();
