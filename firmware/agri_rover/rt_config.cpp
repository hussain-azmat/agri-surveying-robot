// PURE: no Arduino dependencies.
#include "rt_config.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

namespace {

char* trim(char* s) {
  while (*s && isspace((unsigned char)*s)) s++;
  char* e = s + strlen(s);
  while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
  return s;
}

bool parseDouble(const char* s, double* out) {
  if (!*s) return false;
  char* end = nullptr;
  const double v = strtod(s, &end);
  if (end == s || *end != '\0' || !isfinite(v)) return false;
  *out = v;
  return true;
}

// Clamp and count.
template <typename T>
T clampCount(double v, double lo, double hi, RtConfigReport* r) {
  if (v < lo) {
    r->clamped++;
    return (T)lo;
  }
  if (v > hi) {
    r->clamped++;
    return (T)hi;
  }
  return (T)v;
}

}  // namespace

void normalizeRtConfig(RuntimeConfig* c) {
  if (c->offPathM < c->acceptRadiusM + 2.0f) c->offPathM = c->acceptRadiusM + 2.0f;
  if (c->battCritV >= c->battLowV) c->battCritV = c->battLowV - 0.3f;
  if (c->battCritV < 5.0f) c->battCritV = 5.0f;
}

RtConfigReport parseRtConfig(const char* text, RuntimeConfig* cfg) {
  RtConfigReport r;
  if (!text || !cfg) return r;

  char line[128];
  const char* p = text;
  while (*p) {
    size_t n = 0;
    while (p[n] && p[n] != '\n') n++;
    const size_t copy = n < sizeof(line) - 1 ? n : sizeof(line) - 1;
    memcpy(line, p, copy);
    line[copy] = '\0';
    p += n;
    if (*p == '\n') p++;

    char* l = trim(line);
    if (!*l || *l == '#') continue;
    char* eq = strchr(l, '=');
    if (!eq) {
      r.malformed++;
      continue;
    }
    *eq = '\0';
    char* key = trim(l);
    char* val = trim(eq + 1);
    if (!*key) {  // "=5": a value with no name
      r.malformed++;
      continue;
    }
    // allow trailing comments:  key=value  # note
    char* hash = strchr(val, '#');
    if (hash) {
      *hash = '\0';
      val = trim(val);
    }

    double v;
    if (strcmp(key, "logging") == 0) {
      if (strcmp(val, "1") == 0 || strcasecmp(val, "true") == 0 || strcasecmp(val, "on") == 0) cfg->loggingEnabled = true;
      else if (strcmp(val, "0") == 0 || strcasecmp(val, "false") == 0 || strcasecmp(val, "off") == 0) cfg->loggingEnabled = false;
      else {
        r.malformed++;
        continue;
      }
      r.applied++;
      continue;
    }

    if (!parseDouble(val, &v)) {
      // Unknown key with a junk value is "ignored", a known key with a junk value is "malformed".
      static const char* known[] = {"max_speed", "auto_speed", "accept_radius_m", "offpath_m", "cmd_timeout_ms",
                                    "link_timeout_ms", "hdop_max", "batt_low_v", "batt_crit_v", "path_spacing_m",
                                    "path_turn_deg", "upload_idle_s", "sync_interval_s", "ota_interval_s",
                                    "turn_full_dps"};
      bool isKnown = false;
      for (const char* k : known) isKnown |= strcmp(k, key) == 0;
      if (isKnown) r.malformed++;
      else r.ignored++;
      continue;
    }

    if (!strcmp(key, "max_speed")) cfg->maxSpeed = clampCount<float>(v, 0.1, 1.0, &r);
    else if (!strcmp(key, "auto_speed")) cfg->autoSpeed = clampCount<float>(v, 0.1, 0.8, &r);
    else if (!strcmp(key, "accept_radius_m")) cfg->acceptRadiusM = clampCount<float>(v, 3.0, 15.0, &r);
    else if (!strcmp(key, "offpath_m")) cfg->offPathM = clampCount<float>(v, 5.0, 50.0, &r);
    else if (!strcmp(key, "cmd_timeout_ms")) cfg->cmdTimeoutMs = clampCount<uint32_t>(v, 200, 1000, &r);
    else if (!strcmp(key, "link_timeout_ms")) cfg->linkTimeoutMs = clampCount<uint32_t>(v, 1000, 10000, &r);
    else if (!strcmp(key, "hdop_max")) cfg->hdopMax = clampCount<float>(v, 1.0, 20.0, &r);
    else if (!strcmp(key, "batt_low_v")) cfg->battLowV = clampCount<float>(v, 6.0, 30.0, &r);
    else if (!strcmp(key, "batt_crit_v")) cfg->battCritV = clampCount<float>(v, 5.0, 29.0, &r);
    else if (!strcmp(key, "path_spacing_m")) cfg->pathSpacingM = clampCount<float>(v, 1.0, 5.0, &r);
    else if (!strcmp(key, "path_turn_deg")) cfg->pathTurnDeg = clampCount<float>(v, 5.0, 45.0, &r);
    else if (!strcmp(key, "upload_idle_s")) cfg->uploadIdleS = clampCount<uint32_t>(v, 5, 600, &r);
    else if (!strcmp(key, "sync_interval_s")) cfg->syncIntervalS = clampCount<uint32_t>(v, 30, 3600, &r);
    else if (!strcmp(key, "ota_interval_s")) cfg->otaIntervalS = clampCount<uint32_t>(v, 60, 86400, &r);
    else if (!strcmp(key, "turn_full_dps")) cfg->turnFullDps = clampCount<float>(v, 20.0, 360.0, &r);
    else {
      r.ignored++;
      continue;
    }
    r.applied++;
  }
  normalizeRtConfig(cfg);
  return r;
}
