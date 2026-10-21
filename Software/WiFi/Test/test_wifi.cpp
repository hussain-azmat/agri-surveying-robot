// Host unit tests: WiFi (access-code derivation).
#include <string.h>
#include "mini_test.h"
#include "wifi_manager.h"

// ------------------------------------------------------- access code ----

TEST(access_code_matches_the_python_sticker_tool) {
  // Golden vector from `python tools/ap_password.py A4:CF:12:34:56:78` with the default salt.
  // If this fails, tools/ap_password.py and wifi_manager.cpp have drifted apart: stickers would be wrong.
  char code[11];
  deriveAccessCode("agri-rover-v1", "A4CF12345678", code);
  CHECK(strcmp(code, "ue2u86hjju") == 0);
}

TEST(access_code_is_stable_distinct_and_unambiguous) {
  char a[11], b[11], c[11];
  deriveAccessCode("salt", "A4CF12345678", a);
  deriveAccessCode("salt", "A4CF12345678", b);
  deriveAccessCode("salt", "A4CF12345679", c);  // one MAC digit different
  CHECK(strcmp(a, b) == 0);
  CHECK(strcmp(a, c) != 0);
  CHECK(strlen(a) == 10);
  for (const char* p = a; *p; p++) CHECK(strchr("abcdefghjkmnpqrstuvwxyz23456789", *p) != nullptr);
  char d[11];
  deriveAccessCode("other", "A4CF12345678", d);  // changing the salt changes every code
  CHECK(strcmp(a, d) != 0);
}
