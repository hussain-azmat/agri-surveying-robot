// =============================================================================
// Motors bench: runs ONLY the four motor drivers. WHEELS OFF THE GROUND. Nothing moves until you ask.
// Serial monitor at 115200 (line ending "No line ending" or "Newline", either works). Type one character:
//
//   1 2 3 4   spin wheel FL, FR, RL, RR FORWARD for 1 s at 40%     (the same wheel order as config.h)
//   5 6 7 8   spin wheel FL, FR, RL, RR in REVERSE for 1 s
//   s         stop everything now
//
// PASS (see Software/Motors/Readme.txt): each key turns the wheel it names, forward for 1-4 and backward for 5-8.
// A wheel that turns the wrong way: fix WHEEL_INVERT in Software/Common/Src/config.h (or swap that motor's wires).
// =============================================================================
#include "config.h"
#include "github_log.h"
#include "motors.h"

static const char* WHEEL_NAME[W_COUNT] = {"FL", "FR", "RL", "RR"};
static float g_target[W_COUNT] = {0, 0, 0, 0};
static uint32_t g_untilMs = 0;
static uint32_t g_lastUs = 0;

static void stopAll() {
  for (int i = 0; i < W_COUNT; i++) g_target[i] = 0;
  motorsStop();
  g_untilMs = 0;
}

void setup() {
  motorsBegin();  // first: every driver input LOW
  Serial.begin(115200);
  delay(300);
  serialLog("INFO", "Motors bench v" FW_VERSION ": keys 1-4 forward, 5-8 reverse (wheels FL FR RL RR), s = stop. WHEELS OFF THE GROUND.");
  g_lastUs = micros();
}

void loop() {
  while (Serial.available()) {
    const int c = Serial.read();
    if (c == 's' || c == 'S') {
      stopAll();
      serialEvent("STOP", nullptr);
    } else if (c >= '1' && c <= '8') {
      stopAll();
      const int w = (c - '1') % 4;
      const bool reverse = c >= '5';
      g_target[w] = reverse ? -0.4f : 0.4f;
      motorsSetWheels(g_target);
      g_untilMs = millis() + 1000;
      char detail[24];
      snprintf(detail, sizeof(detail), "%s %s", WHEEL_NAME[w], reverse ? "reverse" : "forward");
      serialEvent("SPIN", detail);
    }
  }
  if (g_untilMs && (int32_t)(millis() - g_untilMs) >= 0) {  // 1 s elapsed
    stopAll();
    serialEvent("STOP", "timeout");
  }
  const uint32_t now = micros();
  motorsUpdate((now - g_lastUs) * 1e-6f);
  g_lastUs = now;
  delay(10);
}
