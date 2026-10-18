// PURE: no Arduino dependencies.
#include "ubx.h"

size_t ubxBuild(uint8_t cls, uint8_t id, const uint8_t* payload, uint16_t len, uint8_t* out, size_t cap) {
  const size_t total = (size_t)len + 8;
  if (!out || cap < total) return 0;
  out[0] = 0xB5;
  out[1] = 0x62;
  out[2] = cls;
  out[3] = id;
  out[4] = (uint8_t)(len & 0xFF);
  out[5] = (uint8_t)(len >> 8);
  for (uint16_t i = 0; i < len; i++) out[6 + i] = payload ? payload[i] : 0;
  uint8_t a = 0, b = 0;  // Fletcher-8 over class..payload
  for (size_t i = 2; i < 6 + (size_t)len; i++) {
    a = (uint8_t)(a + out[i]);
    b = (uint8_t)(b + a);
  }
  out[6 + len] = a;
  out[7 + len] = b;
  return total;
}

size_t ubxCfgPrtUart1(uint32_t baud, uint8_t* out, size_t cap) {
  uint8_t p[20] = {0};
  p[0] = 1;  // portID: UART1
  // mode: 8 data bits (3<<6), no parity (4<<9), 1 stop bit (0<<12) = 0x000008D0
  p[4] = 0xD0;
  p[5] = 0x08;
  p[8] = (uint8_t)(baud & 0xFF);
  p[9] = (uint8_t)((baud >> 8) & 0xFF);
  p[10] = (uint8_t)((baud >> 16) & 0xFF);
  p[11] = (uint8_t)((baud >> 24) & 0xFF);
  p[12] = 0x03;  // inProtoMask: UBX + NMEA
  p[14] = 0x02;  // outProtoMask: NMEA only (smaller, and what our parser reads)
  return ubxBuild(0x06, 0x00, p, sizeof(p), out, cap);
}

size_t ubxCfgRate(uint16_t measMs, uint8_t* out, size_t cap) {
  uint8_t p[6];
  p[0] = (uint8_t)(measMs & 0xFF);
  p[1] = (uint8_t)(measMs >> 8);
  p[2] = 1;  // navRate: every measurement
  p[3] = 0;
  p[4] = 1;  // timeRef: GPS time
  p[5] = 0;
  return ubxBuild(0x06, 0x08, p, sizeof(p), out, cap);
}

size_t ubxCfgNmeaRate(uint8_t nmeaId, uint8_t rate, uint8_t* out, size_t cap) {
  const uint8_t p[3] = {0xF0, nmeaId, rate};  // class 0xF0 = standard NMEA messages
  return ubxBuild(0x06, 0x01, p, sizeof(p), out, cap);
}
