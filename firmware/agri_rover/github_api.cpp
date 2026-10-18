#include "github_api.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <Stream.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <mbedtls/base64.h>
#include <time.h>

#include "certs.h"
#include "config.h"
#include "settings.h"

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
