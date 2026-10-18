#include "ota_github.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

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

namespace {


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
    if (strcmp(key, "logging") == 0 || strcmp(key, "auto_update") == 0) {
      bool b;
      if (strcmp(val, "1") == 0 || strcasecmp(val, "true") == 0 || strcasecmp(val, "on") == 0) b = true;
      else if (strcmp(val, "0") == 0 || strcasecmp(val, "false") == 0 || strcasecmp(val, "off") == 0) b = false;
      else {
        r.malformed++;
        continue;
      }
      if (key[0] == 'l') cfg->loggingEnabled = b;
      else cfg->autoUpdate = b;
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

#ifdef ARDUINO
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <Stream.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <mbedtls/base64.h>
#include <time.h>
#include "certs.h"
#include "safety.h"

namespace {

char g_err[96] = "";
uint32_t g_retryAfterS = 0;

constexpr uint32_t kConnectTimeoutMs = 10000;
constexpr uint32_t kReadTimeoutMs = 15000;

void setErr(const char* msg) {
  strncpy(g_err, msg, sizeof(g_err) - 1);
  g_err[sizeof(g_err) - 1] = '\0';
}

// Characters allowed in a repo path. Anything else is rejected, not escaped: our own paths are
// plain ASCII, and a strange name is a bug we want to see rather than paper over.
bool safePath(const char* p) {
  if (!p || !*p || strlen(p) > 160 || strstr(p, "..")) return false;
  for (const char* c = p; *c; c++) {
    const char ch = *c;
    if (!(isalnum((unsigned char)ch) || ch == '/' || ch == '_' || ch == '-' || ch == '.')) return false;
  }
  return true;
}

String contentsUrl(const char* repoPath) {
  String u = "https://api.github.com/repos/";
  u += ghOwner();
  u += "/";
  u += ghRepo();
  u += "/contents/";
  u += repoPath;
  u += "?ref=";
  u += GH_BRANCH;
  return u;
}

// One TLS session object. Heap is only held while a request is in flight.
struct Conn {
  WiFiClientSecure client;
  HTTPClient http;
};

bool prepare(Conn& c, const char* url, bool wantRaw) {
  c.client.setCACert(GITHUB_ROOT_CAS);
  c.client.setHandshakeTimeout(kConnectTimeoutMs / 1000);
  c.http.setConnectTimeout(kConnectTimeoutMs);
  c.http.setTimeout(kReadTimeoutMs);
  c.http.setReuse(false);
  c.http.setUserAgent(String("agri-rover/") + FW_VERSION);
  c.http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  const char* keys[] = {"Retry-After", "X-RateLimit-Remaining"};
  c.http.collectHeaders(keys, 2);
  if (!c.http.begin(c.client, url)) {
    setErr("could not start the request");
    return false;
  }
  c.http.addHeader("Accept", wantRaw ? "application/vnd.github.raw+json" : "application/vnd.github+json");
  c.http.addHeader("X-GitHub-Api-Version", "2022-11-28");
  if (ghTokenPresent()) c.http.addHeader("Authorization", String("Bearer ") + ghToken());
  return true;
}

void noteRateLimit(HTTPClient& http, int code) {
  g_retryAfterS = 0;
  if (code != 403 && code != 429) return;
  if (http.hasHeader("Retry-After")) g_retryAfterS = (uint32_t)http.header("Retry-After").toInt();
  else if (http.hasHeader("X-RateLimit-Remaining") && http.header("X-RateLimit-Remaining").toInt() == 0) g_retryAfterS = 900;
}

void describe(int code) {
  switch (code) {
    case 200:
    case 201:
      g_err[0] = '\0';
      break;
    case 401: setErr("GitHub rejected the token (401): re-enter it on the setup page"); break;
    case 403: setErr(g_retryAfterS ? "GitHub rate limit (403)" : "token lacks permission for this repo (403)"); break;
    case 404: setErr("not found (404): check the repo name and that the token can see it"); break;
    case 422: setErr("already exists (422)"); break;
    case 429: setErr("GitHub rate limit (429)"); break;
    default:
      if (code < 0) snprintf(g_err, sizeof(g_err), "network error: %s", HTTPClient::errorToString(code).c_str());
      else snprintf(g_err, sizeof(g_err), "GitHub returned HTTP %d", code);
  }
}

bool b64(const uint8_t* in, size_t n, String* out) {
  size_t olen = 0;
  mbedtls_base64_encode(nullptr, 0, &olen, in, n);  // reports the required size
  char* tmp = (char*)malloc(olen + 1);
  if (!tmp) return false;
  if (mbedtls_base64_encode((unsigned char*)tmp, olen, &olen, in, n) != 0) {
    free(tmp);
    return false;
  }
  tmp[olen] = '\0';
  *out = tmp;
  free(tmp);
  return true;
}

// Adapts a GhSinkFn to the Stream that HTTPClient::writeToStream expects.
class SinkStream : public Stream {
 public:
  SinkStream(GhSinkFn fn, void* ctx, size_t maxBytes) : fn_(fn), ctx_(ctx), max_(maxBytes) {}
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* b, size_t n) override {
    if (failed_) return 0;
    total_ += n;
    if (total_ > max_) {
      tooBig_ = failed_ = true;
      return 0;
    }
    if (!fn_(b, n, ctx_)) {
      failed_ = true;
      return 0;
    }
    return n;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  bool tooBig() const { return tooBig_; }
  size_t total() const { return total_; }

 private:
  GhSinkFn fn_;
  void* ctx_;
  size_t max_, total_ = 0;
  bool failed_ = false, tooBig_ = false;
};

struct BufSink {
  uint8_t* buf;
  size_t cap, n;
};
bool bufSink(const uint8_t* d, size_t n, void* ctx) {
  BufSink* b = (BufSink*)ctx;
  if (b->n + n > b->cap) return false;
  memcpy(b->buf + b->n, d, n);
  b->n += n;
  return true;
}

struct FileSink {
  File* f;
};
bool fileSink(const uint8_t* d, size_t n, void* ctx) { return ((FileSink*)ctx)->f->write(d, n) == n; }

}  // namespace

bool ghReady() { return WiFi.status() == WL_CONNECTED && time(nullptr) > 1704067200; }
const char* ghLastError() { return g_err; }
uint32_t ghRetryAfterS() { return g_retryAfterS; }

int ghDownload(const char* repoPath, GhSinkFn sink, void* ctx, long expectedSize, size_t maxBytes, size_t* received) {
  if (received) *received = 0;
  g_err[0] = '\0';
  g_retryAfterS = 0;
  if (!safePath(repoPath)) {
    setErr("invalid repo path");
    return -100;
  }
  if (!ghReady()) {
    setErr("offline or clock not set");
    return -101;
  }
  Conn c;
  if (!prepare(c, contentsUrl(repoPath).c_str(), true)) return -102;
  const int code = c.http.GET();
  noteRateLimit(c.http, code);
  describe(code);
  if (code != 200) {
    c.http.end();
    return code;
  }
  const int cl = c.http.getSize();  // -1 when chunked
  if (expectedSize > 0 && cl >= 0 && cl != expectedSize) {
    setErr("server file size does not match the manifest");
    c.http.end();
    return -106;
  }
  SinkStream s(sink, ctx, maxBytes);
  const int w = c.http.writeToStream(&s);
  c.http.end();
  if (received) *received = s.total();
  if (s.tooBig()) {
    setErr("file is larger than allowed");
    return -104;
  }
  if (w < 0) {
    describe(w);
    return w;
  }
  if (expectedSize > 0 && (long)s.total() != expectedSize) {
    setErr("download size does not match the manifest");
    return -106;
  }
  return 200;
}

int ghGetRaw(const char* repoPath, uint8_t* buf, size_t cap, size_t* len) {
  if (len) *len = 0;
  if (cap < 2) return -100;
  BufSink b{buf, cap - 1, 0};
  size_t got = 0;
  const int code = ghDownload(repoPath, bufSink, &b, 0, cap - 1, &got);
  if (code == 200) {
    buf[b.n] = '\0';  // callers treat small files as text
    if (len) *len = b.n;
  }
  return code;
}

int ghGetToFile(const char* repoPath, const char* fsPath, size_t maxBytes) {
  const String part = String(fsPath) + ".part";
  File f = LittleFS.open(part, "w");
  if (!f) {
    setErr("cannot write to flash");
    return -103;
  }
  FileSink fs{&f};
  size_t got = 0;
  const int code = ghDownload(repoPath, fileSink, &fs, 0, maxBytes, &got);
  f.close();
  if (code != 200) {
    LittleFS.remove(part);
    return code;
  }
  LittleFS.remove(fsPath);
  if (!LittleFS.rename(part, fsPath)) {
    setErr("could not finish writing the file");
    return -103;
  }
  return 200;
}

int ghPutFile(const char* repoPath, const uint8_t* data, size_t len, const char* commitMessage, bool overwrite) {
  g_err[0] = '\0';
  g_retryAfterS = 0;
  if (!safePath(repoPath)) {
    setErr("invalid repo path");
    return -100;
  }
  if (!ghReady()) {
    setErr("offline or clock not set");
    return -101;
  }
  if (!ghTokenPresent()) {
    setErr("no GitHub token set: add one on the setup page");
    return 401;
  }

  // When replacing, GitHub needs the current file's blob sha.
  String sha;
  if (overwrite) {
    Conn q;
    if (!prepare(q, contentsUrl(repoPath).c_str(), false)) return -102;
    const int code = q.http.GET();
    noteRateLimit(q.http, code);
    if (code == 200) {
      JsonDocument filter;
      filter["sha"] = true;
      JsonDocument doc;
      deserializeJson(doc, q.http.getStream(), DeserializationOption::Filter(filter));
      sha = doc["sha"].as<String>();
    } else if (code != 404) {
      describe(code);
      q.http.end();
      return code;
    }
    q.http.end();
  }

  String content;
  if (!b64(data, len, &content)) {
    setErr("out of memory");
    return -105;
  }
  String body;
  body.reserve(content.length() + 256);
  body = "{\"message\":\"";
  for (const char* m = commitMessage; *m; m++) {  // commit messages are ours; keep them JSON-safe
    if (*m == '"' || *m == '\\' || (unsigned char)*m < 0x20) body += ' ';
    else body += *m;
  }
  body += "\",\"branch\":\"";
  body += GH_BRANCH;
  body += "\"";
  if (sha.length()) {
    body += ",\"sha\":\"";
    body += sha;
    body += "\"";
  }
  body += ",\"content\":\"";
  body += content;
  body += "\"}";
  content = String();  // free the base64 copy before the TLS buffers are allocated

  Conn c;
  if (!prepare(c, contentsUrl(repoPath).c_str(), false)) return -102;
  c.http.addHeader("Content-Type", "application/json");
  const int code = c.http.PUT((uint8_t*)body.c_str(), body.length());
  noteRateLimit(c.http, code);
  describe(code);
  c.http.end();
  return code;
}

namespace {
Preferences& nvs() {
  static Preferences p;
  static bool opened = false;
  if (!opened) opened = p.begin("gh", false);
  return p;
}
String g_cachedCfgText;
}  // namespace

String ghOwner() {
  const String v = nvs().getString("owner", "");
  return v.length() ? v : String(GH_OWNER_DEFAULT);
}
String ghRepo() {
  const String v = nvs().getString("repo", "");
  return v.length() ? v : String(GH_REPO_DEFAULT);
}
String ghToken() { return nvs().getString("token", ""); }
bool ghTokenPresent() { return nvs().getString("token", "").length() > 0; }
void ghTokenSave(const String& token) { nvs().putString("token", token); }
void ghRepoSave(const String& owner, const String& repo) {
  if (owner.length() && repo.length()) {
    nvs().putString("owner", owner);
    nvs().putString("repo", repo);
  }
}

void otaMarkUpdated(const char* fromVersion) {
  nvs().putString("ota_from", fromVersion ? fromVersion : "?");
  nvs().putBool("ota_new", true);
}
bool otaJustUpdated(String* fromVersion) {
  if (!nvs().getBool("ota_new", false)) return false;
  if (fromVersion) *fromVersion = nvs().getString("ota_from", "");
  return true;
}
void otaClearJustUpdated() { nvs().remove("ota_new"); }

static const char* kCfgFile = "/config/rover.cfg";

RuntimeConfig runtimeConfigLoad() {
  RuntimeConfig c;
  File f = LittleFS.open(kCfgFile, "r");
  if (!f) return c;
  const size_t n = f.size();
  if (n == 0 || n > 4096) {  // a config file is a few hundred bytes; anything else is not ours
    f.close();
    return c;
  }
  String text = f.readString();
  f.close();
  parseRtConfig(text.c_str(), &c);
  g_cachedCfgText = text;
  return c;
}

bool runtimeConfigSync(String* message) {
  String dummy;
  if (!message) message = &dummy;
  LittleFS.mkdir("/config");
  const int rc = ghGetToFile(GH_CONFIG_PATH, "/config/rover.cfg.new", 4096);
  if (rc == 404) {
    *message = "no config/rover.cfg in the repo (using defaults)";
    return false;
  }
  if (rc != 200) {
    *message = String("config fetch failed: ") + ghLastError();
    return false;
  }
  File f = LittleFS.open("/config/rover.cfg.new", "r");
  if (!f) {
    *message = "config download vanished";
    return false;
  }
  String text = f.readString();
  f.close();
  if (text == g_cachedCfgText) {
    LittleFS.remove("/config/rover.cfg.new");
    *message = "config unchanged";
    return false;
  }
  RuntimeConfig c;
  const RtConfigReport r = parseRtConfig(text.c_str(), &c);
  if (r.applied == 0 && r.malformed > 0) {  // looks like garbage (e.g. an HTML error page): keep the old one
    LittleFS.remove("/config/rover.cfg.new");
    *message = "downloaded config has no valid settings; ignored";
    return false;
  }
  LittleFS.remove(kCfgFile);
  LittleFS.rename("/config/rover.cfg.new", kCfgFile);
  g_cachedCfgText = text;
  {
    StateLock l;
    g_state.cfg = c;
  }
  *message = String("config applied (") + r.applied + " values, " + r.clamped + " clamped, " + r.ignored + " ignored)";
  return true;
}

namespace {

OtaStatus g_st = {"idle", 0, "", "", 0xFFFFFFFF, false, true};
uint32_t g_lastCheckMs = 0;
uint32_t g_nextMs = 0;
uint32_t g_backoffMs = 0;
bool g_scheduled = false;  // the power-up check has been scheduled
bool g_busy = false;

void setStatus(const char* state, const char* msg, int pct = 0) {
  strncpy(g_st.state, state, sizeof(g_st.state) - 1);
  g_st.state[sizeof(g_st.state) - 1] = '\0';
  strncpy(g_st.message, msg, sizeof(g_st.message) - 1);
  g_st.message[sizeof(g_st.message) - 1] = '\0';
  g_st.percent = pct;
}

struct FlashCtx {
  size_t written = 0, total = 0;
  int lastPct = -1;
};

bool flashSink(const uint8_t* d, size_t n, void* vctx) {
  FlashCtx* c = (FlashCtx*)vctx;
  if (Update.write((uint8_t*)d, n) != n) return false;  // flash full / write error: abort
  c->written += n;
  const int pct = (int)((c->written * 100ULL) / c->total);
  if (pct != c->lastPct) {
    c->lastPct = pct;
    g_st.percent = pct;
  }
  return true;
}

bool stillSafe() {
  // The flash must never start (or continue) if the rover could be moving.
  return stateSnapshot().mode == Mode::DISARMED;
}

// Returns true if a new image was flashed (the caller then reboots). With install=false it only
// looks: if a newer version exists it is reported as "available" and nothing is downloaded.
bool checkAndUpdate(String* msg, bool install) {
  g_busy = true;
  g_st.available = false;
  g_lastCheckMs = millis();
  setStatus("checking", "checking for a newer version...");

  struct Done {
    bool* busy;
    ~Done() { *busy = false; }
  } done{&g_busy};

  if (!stillSafe()) {
    *msg = "not checking: the rover is armed";
    setStatus("idle", msg->c_str());
    return false;
  }
  if (!ghReady()) {
    *msg = "offline (or clock not set yet)";
    setStatus("error", msg->c_str());
    return false;
  }

  char buf[320];
  size_t n = 0;
  const int rc = ghGetRaw(GH_OTA_VERSION_PATH, (uint8_t*)buf, sizeof(buf), &n);
  if (rc == 404) {
    *msg = "no ota/version.txt in the repo yet";
    setStatus("idle", msg->c_str());
    return false;
  }
  if (rc != 200) {
    *msg = String("could not read version.txt: ") + ghLastError();
    setStatus("error", msg->c_str());
    return false;
  }

  OtaManifest m;
  const char* perr = nullptr;
  if (!parseOtaManifest(buf, &m, &perr)) {
    *msg = String("version.txt is not usable: ") + perr;
    setStatus("error", msg->c_str());
    return false;
  }
  strncpy(g_st.remoteVersion, m.version, sizeof(g_st.remoteVersion) - 1);

  switch (decideOta(FW_VERSION, m.version)) {
    case OtaDecision::UP_TO_DATE:
      *msg = String("up to date (v") + FW_VERSION + ")";
      setStatus("uptodate", msg->c_str());
      return false;
    case OtaDecision::REMOTE_OLDER:
      *msg = String("repo has v") + m.version + ", older than the running v" + FW_VERSION + ": ignored";
      setStatus("uptodate", msg->c_str());
      return false;
    case OtaDecision::INVALID:
      *msg = "version numbers are not valid x.y.z";
      setStatus("error", msg->c_str());
      return false;
    case OtaDecision::UPDATE:
      break;
  }
  g_st.available = true;
  if (!install) {
    *msg = String("update available: v") + m.version + " (running v" + FW_VERSION + ")";
    setStatus("available", msg->c_str());
    return false;
  }

  // ---- A newer version exists: download it into the INACTIVE slot, verifying as we go. ----
  Serial.printf("[ota] updating v%s -> v%s (%lu bytes)\n", FW_VERSION, m.version, (unsigned long)m.size);
  setStatus("downloading", "downloading update...", 0);
  if (!Update.begin(m.size, U_FLASH)) {
    *msg = String("cannot start the update: ") + Update.errorString();
    setStatus("error", msg->c_str());
    Update.abort();
    return false;
  }
  Update.setMD5(m.md5);  // end() refuses to boot the image unless this matches

  FlashCtx ctx;
  ctx.total = m.size;
  size_t got = 0;
  const int drc = ghDownload(GH_OTA_FIRMWARE_PATH, flashSink, &ctx, (long)m.size, m.size + 1024, &got);
  if (drc != 200) {
    Update.abort();
    *msg = String("download failed, old firmware kept: ") + ghLastError();
    setStatus("error", msg->c_str());
    return false;
  }
  if (!stillSafe()) {  // someone armed the rover during the download: do not reboot under them
    Update.abort();
    *msg = "update aborted: the rover was armed during the download";
    setStatus("error", msg->c_str());
    return false;
  }
  if (!Update.end(false)) {  // false: the byte count must equal what begin() was told, and the MD5 must match
    *msg = String("image failed verification, old firmware kept: ") + Update.errorString();
    setStatus("error", msg->c_str());
    return false;
  }

  otaMarkUpdated(FW_VERSION);
  *msg = String("updated to v") + m.version + ", restarting";
  setStatus("idle", msg->c_str(), 100);
  Serial.println("[ota] update verified; restarting into the new firmware (it will stay disarmed)");
  return true;
}

}  // namespace

// The power-up check is scheduled the moment the rover first has WiFi AND a valid clock (see otaService),
// so nothing is polled before that.
void otaBegin() { g_nextMs = millis() + 3600000UL; }

OtaStatus otaStatus() {
  OtaStatus s = g_st;
  s.lastCheckAgoS = g_lastCheckMs ? (millis() - g_lastCheckMs) / 1000 : 0xFFFFFFFF;
  return s;
}

void otaCheckNow(String* message, bool install) {
  String dummy;
  if (!message) message = &dummy;
  if (g_busy) {
    *message = "a check is already running";
    return;
  }
  if (checkAndUpdate(message, install)) {
    delay(400);
    ESP.restart();
  }
}

void otaService(bool online, bool idle) {
  if (g_busy || !online || !idle) return;
  const uint32_t now = millis();
  if (!g_scheduled) {  // first time online since power-up: check right away (3 s for the link to settle)
    g_scheduled = true;
    g_nextMs = now + 3000;
  }
  if ((int32_t)(now - g_nextMs) < 0) return;
  String msg;
  const bool autoInstall = stateSnapshot().cfg.autoUpdate;
  g_st.autoUpdate = autoInstall;
  const bool flashed = checkAndUpdate(&msg, autoInstall);
  Serial.printf("[ota] %s\n", msg.c_str());
  if (flashed) {
    delay(400);
    ESP.restart();
    return;
  }
  const uint32_t interval = stateSnapshot().cfg.otaIntervalS * 1000UL;
  if (strcmp(g_st.state, "error") == 0) {
    g_backoffMs = g_backoffMs ? min<uint32_t>(g_backoffMs * 2, interval) : 30000;
    if (ghRetryAfterS()) g_backoffMs = max<uint32_t>(g_backoffMs, ghRetryAfterS() * 1000UL);
    g_nextMs = now + g_backoffMs;
  } else {
    g_backoffMs = 0;
    g_nextMs = now + interval;
  }
}
#endif
