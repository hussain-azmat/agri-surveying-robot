// PURE: no Arduino dependencies.
#include "accesscode.h"

#include <stdint.h>
#include <string.h>

namespace {
uint32_t fnv1a(const char* s, uint32_t seed) {
  uint32_t h = seed;
  for (; *s; s++) {
    h ^= (uint8_t)*s;
    h *= 16777619u;
  }
  return h;
}
}  // namespace

void deriveAccessCode(const char* salt, const char* macHex12, char* out) {
  // Unambiguous alphabet (no 0/o, 1/l/i) so it can be read off a sticker without mistakes.
  static const char alphabet[] = "abcdefghjkmnpqrstuvwxyz23456789";
  char seed[96];
  strncpy(seed, salt, sizeof(seed) - 16);
  seed[sizeof(seed) - 16] = '\0';
  strcat(seed, ":");
  strncat(seed, macHex12, 12);
  const uint32_t a = fnv1a(seed, 2166136261u);
  const uint32_t b = fnv1a(seed, 0x9E3779B1u);
  for (int i = 0; i < 10; i++) {
    const uint32_t v = i < 5 ? a : b;
    out[i] = alphabet[(v >> ((i % 5) * 6)) % 31];
  }
  out[10] = '\0';
}
