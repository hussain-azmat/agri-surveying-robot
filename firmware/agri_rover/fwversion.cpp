// PURE: no Arduino dependencies.
#include "fwversion.h"

#include <ctype.h>
#include <string.h>

namespace {

bool parseNum(const char*& p, uint16_t* out) {
  if (!isdigit((unsigned char)*p)) return false;
  uint32_t v = 0;
  while (isdigit((unsigned char)*p)) {
    v = v * 10 + (uint32_t)(*p - '0');
    if (v > 65535) return false;
    p++;
  }
  *out = (uint16_t)v;
  return true;
}

// Trims leading/trailing whitespace in place; returns the start of the trimmed string.
char* trim(char* s) {
  while (*s && isspace((unsigned char)*s)) s++;
  char* e = s + strlen(s);
  while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
  return s;
}

}  // namespace

bool parseSemVer(const char* s, SemVer* out) {
  if (!s) return false;
  if (*s == 'v' || *s == 'V') s++;
  SemVer v;
  const char* p = s;
  if (!parseNum(p, &v.major) || *p++ != '.') return false;
  if (!parseNum(p, &v.minor) || *p++ != '.') return false;
  if (!parseNum(p, &v.patch)) return false;
  if (*p != '\0') return false;
  if (out) *out = v;
  return true;
}

int compareSemVer(const SemVer& a, const SemVer& b) {
  if (a.major != b.major) return a.major < b.major ? -1 : 1;
  if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
  if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
  return 0;
}

bool parseOtaManifest(const char* text, OtaManifest* out, const char** err) {
  const char* dummy;
  if (!err) err = &dummy;
  if (!text || !out) {
    *err = "no manifest";
    return false;
  }
  OtaManifest m;
  bool haveVersion = false, haveMd5 = false, haveSize = false;

  char line[96];
  const char* p = text;
  while (*p) {
    size_t n = 0;
    while (p[n] && p[n] != '\n') n++;
    if (n >= sizeof(line)) {
      *err = "manifest line too long";
      return false;
    }
    memcpy(line, p, n);
    line[n] = '\0';
    p += n;
    if (*p == '\n') p++;

    char* l = trim(line);
    if (!*l || *l == '#') continue;
    char* eq = strchr(l, '=');
    if (!eq) continue;
    *eq = '\0';
    char* key = trim(l);
    char* val = trim(eq + 1);

    if (strcmp(key, "version") == 0) {
      SemVer v;
      if (!parseSemVer(val, &v) || strlen(val) >= sizeof(m.version)) {
        *err = "bad version";
        return false;
      }
      strncpy(m.version, (*val == 'v' || *val == 'V') ? val + 1 : val, sizeof(m.version) - 1);
      haveVersion = true;
    } else if (strcmp(key, "md5") == 0) {
      if (strlen(val) != 32) {
        *err = "md5 must be 32 hex characters";
        return false;
      }
      for (int i = 0; i < 32; i++) {
        if (!isxdigit((unsigned char)val[i])) {
          *err = "md5 must be hex";
          return false;
        }
        m.md5[i] = (char)tolower((unsigned char)val[i]);
      }
      m.md5[32] = '\0';
      haveMd5 = true;
    } else if (strcmp(key, "size") == 0) {
      uint64_t s = 0;
      if (!*val) {
        *err = "bad size";
        return false;
      }
      for (const char* q = val; *q; q++) {
        if (!isdigit((unsigned char)*q)) {
          *err = "bad size";
          return false;
        }
        s = s * 10 + (uint64_t)(*q - '0');
        if (s > 16u * 1024u * 1024u) {
          *err = "size is implausibly large";
          return false;
        }
      }
      if (s < 64 * 1024) {
        *err = "size is implausibly small";
        return false;
      }
      m.size = (uint32_t)s;
      haveSize = true;
    }
  }
  if (!haveVersion) {
    *err = "manifest has no version";
    return false;
  }
  if (!haveMd5) {
    *err = "manifest has no md5 (refusing to flash an unverifiable image)";
    return false;
  }
  if (!haveSize) {
    *err = "manifest has no size";
    return false;
  }
  *out = m;
  return true;
}

OtaDecision decideOta(const char* current, const char* remote) {
  SemVer c, r;
  if (!parseSemVer(current, &c) || !parseSemVer(remote, &r)) return OtaDecision::INVALID;
  const int cmp = compareSemVer(r, c);
  if (cmp > 0) return OtaDecision::UPDATE;
  if (cmp == 0) return OtaDecision::UP_TO_DATE;
  return OtaDecision::REMOTE_OLDER;
}
