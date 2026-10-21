#include "github_log.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

// ================================================================================================
// Serial JSON formatters (plain C++, no Arduino dependency)
// ================================================================================================
namespace {
double fin(double v) { return isfinite(v) ? v : 0.0; }  // JSON has no NaN/inf
}  // namespace

size_t jsonEscape(const char* in, char* out, size_t cap) {
  if (!out || cap == 0) return 0;
  size_t n = 0;
  for (; in && *in; in++) {
    const unsigned char c = (unsigned char)*in;
    char tmp[8];
    size_t len;
    if (c == '"' || c == '\\') {
      tmp[0] = '\\';
      tmp[1] = (char)c;
      len = 2;
    } else if (c == '\n') {
      memcpy(tmp, "\\n", 2);
      len = 2;
    } else if (c == '\r') {
      memcpy(tmp, "\\r", 2);
      len = 2;
    } else if (c == '\t') {
      memcpy(tmp, "\\t", 2);
      len = 2;
    } else if (c < 0x20) {
      len = (size_t)snprintf(tmp, sizeof(tmp), "\\u%04x", c);
    } else {
      tmp[0] = (char)c;
      len = 1;
    }
    if (n + len >= cap) break;  // never write past the buffer, never cut an escape in half
    memcpy(out + n, tmp, len);
    n += len;
  }
  out[n] = '\0';
  return n;
}

static size_t finish(char* out, size_t cap, int written) {
  if (written < 0) {
    if (cap) out[0] = '\0';
    return 0;
  }
  return (size_t)written >= cap ? cap - 1 : (size_t)written;
}

size_t formatImuJson(char* out, size_t cap, const ImuData& d) {
  return finish(out, cap,
                snprintf(out, cap,
                         "{\"imu\":{\"accel_x\":%.2f,\"accel_y\":%.2f,\"accel_z\":%.2f,"
                         "\"gyro_x\":%.2f,\"gyro_y\":%.2f,\"gyro_z\":%.2f}}",
                         fin(d.accelX), fin(d.accelY), fin(d.accelZ), fin(d.gyroX), fin(d.gyroY), fin(d.gyroZ)));
}

size_t formatGpsJson(char* out, size_t cap, const GpsData& g) {
  if (g.fix) {
    return finish(out, cap,
                  snprintf(out, cap,
                           "{\"gps\":{\"fix\":true,\"quality\":%u,\"sats\":%u,\"hdop\":%.1f,\"lat\":%.7f,\"lon\":%.7f,"
                           "\"speed_mps\":%.2f,\"course_deg\":%.1f}}",
                           (unsigned)g.quality, (unsigned)g.sats, fin(g.hdop), fin(g.lat), fin(g.lon), fin(g.speedMps),
                           fin(g.courseDeg)));
  }
  return finish(out, cap,
                snprintf(out, cap, "{\"gps\":{\"fix\":false,\"quality\":%u,\"sats\":%u,\"hdop\":%.1f}}",
                         (unsigned)g.quality, (unsigned)g.sats, fin(g.hdop)));
}

size_t formatStatusJson(char* out, size_t cap, const char* status, const char* state, const char* reason) {
  char s[40], st[40], r[60];
  jsonEscape(status ? status : "", s, sizeof(s));
  jsonEscape(state ? state : "", st, sizeof(st));
  jsonEscape(reason ? reason : "", r, sizeof(r));
  int w = snprintf(out, cap, "{\"status\":\"%s\"", s);
  if (w > 0 && (size_t)w < cap && state) w += snprintf(out + w, cap - w, ",\"state\":\"%s\"", st);
  if (w > 0 && (size_t)w < cap && reason) w += snprintf(out + w, cap - w, ",\"reason\":\"%s\"", r);
  if (w > 0 && (size_t)w < cap) w += snprintf(out + w, cap - w, "}");
  return finish(out, cap, w);
}

size_t formatMessageJson(char* out, size_t cap, const char* status, const char* message) {
  char s[40], m[260];
  jsonEscape(status ? status : "", s, sizeof(s));
  jsonEscape(message ? message : "", m, sizeof(m));
  return finish(out, cap, snprintf(out, cap, "{\"status\":\"%s\",\"message\":\"%s\"}", s, m));
}

const char* logHeader() {
  return "utc,uptime_ms,lat,lon,fix,sats,hdop,speed_mps,course_deg,yaw_deg,mode,batt_v,xte_m,wp,event,"
         "accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z,pitch_deg,roll_deg\n";
}

size_t formatLogRow(char* out, size_t cap, const char* utc, uint32_t uptimeMs, const char* mode, bool battKnown,
                    float battV, bool autoMode, float xteM, int wp, const GpsData& g, const ImuData& i,
                    const char* event) {
  char batt[12] = "", xte[12] = "", wps[12] = "", lat[20] = "", lon[20] = "";
  if (battKnown) snprintf(batt, sizeof(batt), "%.2f", fin(battV));
  if (autoMode) {
    snprintf(xte, sizeof(xte), "%.2f", fin(xteM));
    snprintf(wps, sizeof(wps), "%d", wp);
  }
  if (g.fix) {
    snprintf(lat, sizeof(lat), "%.7f", fin(g.lat));
    snprintf(lon, sizeof(lon), "%.7f", fin(g.lon));
  }
  char imu[112];
  if (i.ok) {
    snprintf(imu, sizeof(imu), "%.2f,%.2f,%.2f,%.3f,%.3f,%.3f,%.1f,%.1f", fin(i.accelX), fin(i.accelY), fin(i.accelZ),
             fin(i.gyroX), fin(i.gyroY), fin(i.gyroZ), fin(i.pitchDeg), fin(i.rollDeg));
  } else {
    strcpy(imu, ",,,,,,,");  // eight empty fields: IMU not answering
  }
  const int n = snprintf(out, cap, "%s,%lu,%s,%s,%u,%u,%.1f,%.2f,%.0f,%.1f,%s,%s,%s,%s,%s,%s\n", utc ? utc : "",
                         (unsigned long)uptimeMs, lat, lon, (unsigned)g.quality, (unsigned)g.sats, fin(g.hdop),
                         fin(g.speedMps), g.courseValid ? fin(g.courseDeg) : 0.0, i.yawValid ? fin(i.yawDeg) : 0.0,
                         mode ? mode : "", batt, xte, wps, event ? event : "", imu);
  if (n <= 0 || (size_t)n >= cap) {
    if (cap) out[0] = '\0';
    return 0;
  }
  return (size_t)n;
}

size_t formatEventJson(char* out, size_t cap, const char* event, const char* detail) {
  char e[60], d[120];
  jsonEscape(event ? event : "", e, sizeof(e));
  jsonEscape(detail ? detail : "", d, sizeof(d));
  if (detail) return finish(out, cap, snprintf(out, cap, "{\"event\":\"%s\",\"detail\":\"%s\"}", e, d));
  return finish(out, cap, snprintf(out, cap, "{\"event\":\"%s\"}", e));
}

#ifdef ARDUINO
#include <LittleFS.h>
#include <Preferences.h>
#include <sys/time.h>
#include <time.h>
#include "config.h"
#include "ota_github.h"
#include "wifi_manager.h"

namespace {

Preferences& nvs() {
  static Preferences p;
  static bool opened = false;
  if (!opened) opened = p.begin("log", false);
  return p;
}
uint32_t nvsGet(const char* key, uint32_t def) { return nvs().getUInt(key, def); }
void nvsPut(const char* key, uint32_t value) { nvs().putUInt(key, value); }


char g_buf[2048];
size_t g_bufN = 0;
File g_cur;
bool g_curOpen = false;
char g_curDate[9] = "00000000";  // YYYYMMDD of the first row in cur.csv, "00000000" if the clock was not set
uint32_t g_lastRowMs = 0;
uint32_t g_rows = 0;
bool g_full = false;
char g_err[96] = "";
uint32_t g_lastUploadMs = 0;
uint32_t g_backoffMs = 0, g_nextTryMs = 0;
uint32_t g_seq = 0;
bool g_began = false;

void setErr(const char* m) {
  strncpy(g_err, m, sizeof(g_err) - 1);
  g_err[sizeof(g_err) - 1] = '\0';
}

uint32_t freeBytes() { return LittleFS.totalBytes() - LittleFS.usedBytes(); }

bool openCur() {
  if (g_curOpen) return true;
  LittleFS.mkdir("/log");
  LittleFS.mkdir("/log/q");
  g_cur = LittleFS.open("/log/cur.csv", FILE_APPEND);
  if (!g_cur) return false;
  g_curOpen = true;
  if (g_cur.size() == 0) g_cur.print(logHeader());
  return true;
}

void flushBuf() {
  if (g_bufN == 0) return;
  if (openCur()) {
    if (g_cur.write((const uint8_t*)g_buf, g_bufN) != g_bufN) setErr("flash write failed");
    g_cur.flush();
  }
  g_bufN = 0;
}

void closeCur() {
  flushBuf();
  if (g_curOpen) {
    g_cur.close();
    g_curOpen = false;
  }
}

// Moves cur.csv into the upload queue. Returns true if something was queued.
bool rotate() {
  closeCur();
  File f = LittleFS.open("/log/cur.csv", "r");
  if (!f) return false;
  const size_t size = f.size();
  f.close();
  if (size <= strlen(logHeader())) {  // header only: nothing worth uploading
    LittleFS.remove("/log/cur.csv");
    return false;
  }
  char name[48];
  snprintf(name, sizeof(name), "/log/q/%06lu_%s.csv", (unsigned long)g_seq, g_curDate);
  LittleFS.mkdir("/log/q");
  if (!LittleFS.rename("/log/cur.csv", name)) return false;
  g_seq++;
  nvsPut("lseq", g_seq);
  strcpy(g_curDate, "00000000");
  return true;
}

// "2026-10-18T07:21:05.200Z" from the system clock; empty if the clock is not set.
void utcNow(char* out, size_t cap, char* dateOut) {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  if (tv.tv_sec < 1704067200) {
    out[0] = '\0';
    if (dateOut) strcpy(dateOut, "00000000");
    return;
  }
  struct tm t;
  gmtime_r(&tv.tv_sec, &t);
  // Fields are range-limited so the output length is provably bounded (and the compiler can see it).
  const unsigned y = (unsigned)(t.tm_year + 1900) % 10000, mo = (unsigned)(t.tm_mon + 1) % 100,
                 d = (unsigned)t.tm_mday % 100, h = (unsigned)t.tm_hour % 100, mi = (unsigned)t.tm_min % 100,
                 s = (unsigned)t.tm_sec % 100, ms = (unsigned)(tv.tv_usec / 1000) % 1000;
  snprintf(out, cap, "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", y, mo, d, h, mi, s, ms);
  if (dateOut) {
    char tmp[16];
    snprintf(tmp, sizeof(tmp), "%04u%02u%02u", y, mo, d);
    memcpy(dateOut, tmp, 8);
    dateOut[8] = '\0';
  }
}

void appendRow(const RoverState& s, const char* event) {
  if (!g_began) return;
  if (freeBytes() < LOG_FS_RESERVE_BYTES) {
    g_full = true;
    return;
  }
  g_full = false;

  char utc[40];
  char date[9];
  utcNow(utc, sizeof(utc), date);
  if (strcmp(g_curDate, "00000000") == 0 && strcmp(date, "00000000") != 0) strcpy(g_curDate, date);

  const char* mode = s.mode == Mode::AUTO ? "AUTO" : (s.mode == Mode::MANUAL ? (s.recording ? "REC" : "MAN") : "OFF");
  char line[320];
  const size_t n = formatLogRow(line, sizeof(line), utc, millis(), mode, s.battKnown, s.battV, s.mode == Mode::AUTO,
                                s.follow.xteM, s.follow.wpIndex, s.gps, s.imu, event);
  if (n == 0) return;
  if (g_bufN + n > sizeof(g_buf)) flushBuf();
  memcpy(g_buf + g_bufN, line, n);
  g_bufN += n;
  g_rows++;
}

// Oldest queued file name (they sort by their zero-padded sequence number), or "".
String oldestQueued(uint32_t* count, uint32_t* bytes) {
  String best;
  *count = 0;
  *bytes = 0;
  File dir = LittleFS.open("/log/q");
  if (!dir || !dir.isDirectory()) return best;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String n = f.name();
    if (!n.endsWith(".csv")) continue;
    (*count)++;
    *bytes += f.size();
    if (best.length() == 0 || n < best) best = n;
  }
  return best;
}

bool isGood(int rc) { return rc == 200 || rc == 201 || rc == 422; }  // 422: that chunk already exists

}  // namespace

void logBegin() {
  LittleFS.mkdir("/log");
  LittleFS.mkdir("/log/q");
  g_seq = nvsGet("lseq", 0);
  g_began = true;
  // Anything left in cur.csv is from before a reboot: queue it as it is. A torn last line (power cut
  // mid-write) is dropped at upload time, because only complete lines are ever sent.
  File f = LittleFS.open("/log/cur.csv", "r");
  if (f) {
    f.readStringUntil('\n');            // header
    const String first = f.readStringUntil('\n');  // first data row, to learn its date
    f.close();
    if (first.length() >= 10 && first[4] == '-' && first[7] == '-') {
      snprintf(g_curDate, sizeof(g_curDate), "%.4s%.2s%.2s", first.c_str(), first.c_str() + 5, first.c_str() + 8);
    }
    rotate();
  }
}

void logTick(const RoverState& s) {
  if (!g_began || s.mode == Mode::DISARMED || !s.cfg.loggingEnabled) return;
  const uint32_t now = millis();
  if (now - g_lastRowMs < LOG_PERIOD_MS) return;
  g_lastRowMs = now;
  appendRow(s, "");
  // Write to flash about once a second; the buffer holds ~12 rows, so a power cut loses < 1 s.
  static uint32_t lastFlush = 0;
  if (now - lastFlush > 1000) {
    lastFlush = now;
    flushBuf();
  }
}

void logEvent(const RoverState& s, const char* event) {
  if (!g_began || !s.cfg.loggingEnabled) return;
  appendRow(s, event);
  flushBuf();
}

void logFlush() { flushBuf(); }

LogStatus logStatus() {
  LogStatus st;
  uint32_t count = 0, bytes = 0;
  oldestQueued(&count, &bytes);
  uint32_t curBytes = g_bufN;
  if (g_curOpen) curBytes += g_cur.size();
  else {
    File f = LittleFS.open("/log/cur.csv", "r");
    if (f) {
      curBytes += f.size();
      f.close();
    }
  }
  st.pendingBytes = bytes + curBytes;
  st.queuedFiles = count;
  st.rowsLogged = g_rows;
  st.full = g_full;
  strncpy(st.lastError, g_err, sizeof(st.lastError) - 1);
  st.lastError[sizeof(st.lastError) - 1] = '\0';
  st.lastUploadAgoS = g_lastUploadMs ? (millis() - g_lastUploadMs) / 1000 : 0xFFFFFFFF;
  return st;
}

void logService(bool online, bool idle) {
  if (!g_began || !online || !idle || !ghTokenPresent()) return;
  const uint32_t now = millis();
  if ((int32_t)(now - g_nextTryMs) < 0) return;

  uint32_t count, bytes;
  String name = oldestQueued(&count, &bytes);
  if (name.length() == 0) {
    if (!rotate()) return;  // nothing recorded since the last upload
    name = oldestQueued(&count, &bytes);
    if (name.length() == 0) return;
  }

  // Parse "<seq6>_<yyyymmdd>.csv"
  if (name.length() < 19) {
    LittleFS.remove(String("/log/q/") + name);  // not ours
    return;
  }
  const uint32_t fileSeq = (uint32_t)name.substring(0, 6).toInt();
  const String ymd = name.substring(7, 15);
  String folder = "unsynced";
  if (ymd != "00000000") folder = ymd.substring(0, 4) + "-" + ymd.substring(4, 6) + "-" + ymd.substring(6, 8);

  // Resume point for the head file (kept in flash, so a reboot mid-upload continues, not restarts).
  uint32_t off = 0, idx = 0;
  if (nvsGet("lq_seq", 0xFFFFFFFF) == fileSeq) {
    off = nvsGet("lq_off", 0);
    idx = nvsGet("lq_idx", 0);
  }

  const String full = String("/log/q/") + name;
  File f = LittleFS.open(full, "r");
  if (!f) return;
  const size_t fileSize = f.size();
  if (off >= fileSize) {  // finished (or empty): drop it
    f.close();
    LittleFS.remove(full);
    nvsPut("lq_seq", 0xFFFFFFFF);
    return;
  }
  // Every chunk after the first carries the file's OWN header line, so a file written by an older firmware (fewer
  // columns) is never given the new header.
  char hdr[256];
  f.seek(0);
  size_t hdrLen = f.readBytesUntil('\n', hdr, sizeof(hdr) - 2);
  hdr[hdrLen++] = '\n';
  f.seek(off);

  const size_t cap = LOG_UPLOAD_CHUNK_BYTES;
  uint8_t* buf = (uint8_t*)malloc(cap + hdrLen);
  if (!buf) {
    f.close();
    setErr("out of memory");
    return;
  }
  size_t used = 0;
  if (off > 0) {  // every chunk must stand alone, so it carries its own header
    memcpy(buf, hdr, hdrLen);
    used = hdrLen;
  }
  const size_t got = f.read(buf + used, cap);
  f.close();

  // Send whole lines only: cut at the last newline.
  size_t end = used + got;
  while (end > used && buf[end - 1] != '\n') end--;
  const size_t consumed = end - used;
  if (consumed == 0) {  // no complete line left: a torn tail from a power cut. Drop the remainder.
    free(buf);
    LittleFS.remove(full);
    nvsPut("lq_seq", 0xFFFFFFFF);
    return;
  }

  char repoPath[128];
  snprintf(repoPath, sizeof(repoPath), "%s/%s/%s/%06lu_%03lu.csv", GH_LOGS_DIR, roverId().c_str(), folder.c_str(),
           (unsigned long)fileSeq, (unsigned long)idx);
  char msg[96];
  snprintf(msg, sizeof(msg), "log %s %s #%lu.%lu", roverId().c_str(), folder.c_str(), (unsigned long)fileSeq,
           (unsigned long)idx);

  const int rc = ghPutFile(repoPath, buf, end, msg, false);
  free(buf);

  if (isGood(rc)) {
    g_err[0] = '\0';
    g_backoffMs = 0;
    g_nextTryMs = 0;
    g_lastUploadMs = millis();
    off += consumed;
    nvsPut("lq_seq", fileSeq);
    nvsPut("lq_off", off);
    nvsPut("lq_idx", idx + 1);
    if (off >= fileSize) {
      LittleFS.remove(full);
      nvsPut("lq_seq", 0xFFFFFFFF);
    }
    return;
  }

  // ---- Failure: back off, never hammer GitHub. ----
  setErr(ghLastError());
  if (rc == 401 || rc == 404 || (rc == 403 && ghRetryAfterS() == 0)) {
    g_backoffMs = 1800000;  // a configuration problem (token/permissions/repo): minutes will not fix it
  } else {
    g_backoffMs = g_backoffMs ? min<uint32_t>(g_backoffMs * 2, 600000) : 10000;
    if (ghRetryAfterS()) g_backoffMs = max<uint32_t>(g_backoffMs, ghRetryAfterS() * 1000UL);
  }
  g_nextTryMs = millis() + g_backoffMs;
  serialLog("WARN", "log upload failed (%d): %s; retry in %lu s", rc, g_err, (unsigned long)(g_backoffMs / 1000));
}

// ================================================================================================
// Serial output (Arduino)
// ================================================================================================
static void emitLine(char* line, size_t len) {
  if (len + 1 >= 400) len = 398;
  line[len++] = '\n';
  Serial.write((const uint8_t*)line, len);  // ONE write per line: the control task and the main loop never interleave
}

void serialLog(const char* status, const char* fmt, ...) {
  char msg[260];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  char line[400];
  emitLine(line, formatMessageJson(line, sizeof(line) - 1, status, msg));
}

void serialStatus(const char* status, const char* state, const char* reason) {
  char line[400];
  emitLine(line, formatStatusJson(line, sizeof(line) - 1, status, state, reason));
}

void serialEvent(const char* event, const char* detail) {
  char line[400];
  emitLine(line, formatEventJson(line, sizeof(line) - 1, event, detail));
}

void serialTelemetry(const RoverState& s) {
#if SERIAL_TELEMETRY
  static uint32_t lastImu = 0, lastGps = 0;
  const uint32_t now = millis();
  char line[400];
  if (now - lastImu >= SERIAL_IMU_PERIOD_MS) {
    lastImu = now;
    if (s.imu.ok) emitLine(line, formatImuJson(line, sizeof(line) - 1, s.imu));
  }
  if (now - lastGps >= SERIAL_GPS_PERIOD_MS) {
    lastGps = now;
    emitLine(line, formatGpsJson(line, sizeof(line) - 1, s.gps));
  }
#else
  (void)s;
#endif
}
#endif
