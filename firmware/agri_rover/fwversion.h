#pragma once
// PURE: no Arduino dependencies. Unit-tested on a laptop (test/test_misc.cpp).
//
// Version handling for OTA. The brief: "update only if the repo version is NEWER, not
// merely different". ota/version.txt is plain text:
//
//     version=1.2.3
//     md5=0123456789abcdef0123456789abcdef
//     size=1048576
//
// (key=value, one per line, '#' comments and blank lines allowed, CRLF tolerated.)
#include <stddef.h>
#include <stdint.h>

struct SemVer {
  uint16_t major = 0, minor = 0, patch = 0;
};

// Accepts "1.2.3" or "v1.2.3". Rejects anything else (extra parts, signs, blanks, overflow).
bool parseSemVer(const char* s, SemVer* out);
// <0 if a<b, 0 if equal, >0 if a>b.
int compareSemVer(const SemVer& a, const SemVer& b);

struct OtaManifest {
  char version[16] = {0};
  char md5[33] = {0};  // lower-case hex
  uint32_t size = 0;
};

// Returns false (and sets *err to a static string) if the manifest is unusable.
bool parseOtaManifest(const char* text, OtaManifest* out, const char** err);

enum class OtaDecision : uint8_t { UPDATE, UP_TO_DATE, REMOTE_OLDER, INVALID };
// `current` is the running firmware version, `remote` comes from the manifest.
OtaDecision decideOta(const char* current, const char* remote);
