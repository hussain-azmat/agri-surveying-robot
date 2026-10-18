#pragma once
// PURE: no Arduino dependencies. Builds u-blox UBX configuration frames for the NEO-6M.
//
// The NEO-6M boots at 9600 baud / 1 Hz with a full set of NMEA sentences. 5 Hz of that
// does not fit through 9600 baud, so at start-up we (1) raise the baud rate, (2) set the
// measurement rate to 200 ms, (3) switch off the sentences we do not use (GLL, GSA, GSV,
// VTG), keeping GGA + RMC. Nothing is saved to the module's flash: it is re-applied on
// every boot, so a replaced or reset module needs no manual setup.
#include <stddef.h>
#include <stdint.h>

// Generic frame: sync, class, id, little-endian length, payload, Fletcher checksum.
// Returns the frame length, or 0 if `cap` is too small.
size_t ubxBuild(uint8_t cls, uint8_t id, const uint8_t* payload, uint16_t len, uint8_t* out, size_t cap);

// UBX-CFG-PRT for UART1: 8N1, UBX+NMEA in, NMEA out, at `baud`.
size_t ubxCfgPrtUart1(uint32_t baud, uint8_t* out, size_t cap);
// UBX-CFG-RATE: measurement period in ms (200 = 5 Hz), 1 measurement per navigation solution.
size_t ubxCfgRate(uint16_t measMs, uint8_t* out, size_t cap);
// UBX-CFG-MSG: output rate (0 = off, 1 = every solution) of one NMEA sentence on the current port.
// nmeaId: 0x00 GGA, 0x01 GLL, 0x02 GSA, 0x03 GSV, 0x04 RMC, 0x05 VTG
size_t ubxCfgNmeaRate(uint8_t nmeaId, uint8_t rate, uint8_t* out, size_t cap);

constexpr uint8_t NMEA_ID_GGA = 0x00, NMEA_ID_GLL = 0x01, NMEA_ID_GSA = 0x02, NMEA_ID_GSV = 0x03,
                  NMEA_ID_RMC = 0x04, NMEA_ID_VTG = 0x05;
