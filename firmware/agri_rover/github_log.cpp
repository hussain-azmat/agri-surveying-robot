#include "github_log.h"

#include <LittleFS.h>
#include <sys/time.h>
#include <time.h>

#include "config.h"
#include "github_api.h"
#include "settings.h"

namespace {

const char* kHeader =
    "utc,uptime_ms,lat,lon,fix,sats,hdop,speed_mps,course_deg,yaw_deg,mode,batt_v,xte_m,wp,event\n";

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
  if (g_cur.size() == 0) g_cur.print(kHeader);
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
  if (size <= strlen(kHeader)) {  // header only: nothing worth uploading
    LittleFS.remove("/log/cur.csv");
    return false;
  }
  char name[48];
  snprintf(name, sizeof(name), "/log/q/%06lu_%s.csv", (unsigned long)g_seq, g_curDate);
  LittleFS.mkdir("/log/q");
  if (!LittleFS.rename("/log/cur.csv", name)) return false;
  g_seq++;
  nvsPutU32("lseq", g_seq);
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
  char batt[12] = "";
  if (s.battKnown) snprintf(batt, sizeof(batt), "%.2f", s.battV);
  char xte[12] = "", wp[12] = "";
  if (s.mode == Mode::AUTO) {
    snprintf(xte, sizeof(xte), "%.2f", s.follow.xteM);
    snprintf(wp, sizeof(wp), "%d", s.follow.wpIndex);
  }
  char lat[20] = "", lon[20] = "";
  if (s.gps.fix) {
    snprintf(lat, sizeof(lat), "%.7f", s.gps.lat);
    snprintf(lon, sizeof(lon), "%.7f", s.gps.lon);
  }
  char line[220];
  const int n = snprintf(line, sizeof(line), "%s,%lu,%s,%s,%u,%u,%.1f,%.2f,%.0f,%.1f,%s,%s,%s,%s,%s\n", utc,
                         (unsigned long)millis(), lat, lon, (unsigned)s.gps.quality, (unsigned)s.gps.sats, s.gps.hdop,
                         s.gps.speedMps, s.gps.courseValid ? s.gps.courseDeg : 0.0f, s.imu.yawValid ? s.imu.yawDeg : 0.0f,
                         mode, batt, xte, wp, event ? event : "");
  if (n <= 0 || n >= (int)sizeof(line)) return;
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
  g_seq = nvsGetU32("lseq", 0);
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
  if (nvsGetU32("lq_seq", 0xFFFFFFFF) == fileSeq) {
    off = nvsGetU32("lq_off", 0);
    idx = nvsGetU32("lq_idx", 0);
  }

  const String full = String("/log/q/") + name;
  File f = LittleFS.open(full, "r");
  if (!f) return;
  const size_t fileSize = f.size();
  if (off >= fileSize) {  // finished (or empty): drop it
    f.close();
    LittleFS.remove(full);
    nvsPutU32("lq_seq", 0xFFFFFFFF);
    return;
  }
  f.seek(off);

  const size_t hdrLen = strlen(kHeader);
  const size_t cap = LOG_UPLOAD_CHUNK_BYTES;
  uint8_t* buf = (uint8_t*)malloc(cap + hdrLen);
  if (!buf) {
    f.close();
    setErr("out of memory");
    return;
  }
  size_t used = 0;
  if (off > 0) {  // every chunk must stand alone, so it carries its own header
    memcpy(buf, kHeader, hdrLen);
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
    nvsPutU32("lq_seq", 0xFFFFFFFF);
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
    nvsPutU32("lq_seq", fileSeq);
    nvsPutU32("lq_off", off);
    nvsPutU32("lq_idx", idx + 1);
    if (off >= fileSize) {
      LittleFS.remove(full);
      nvsPutU32("lq_seq", 0xFFFFFFFF);
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
  Serial.printf("[log] upload failed (%d): %s; retry in %lu s\n", rc, g_err, (unsigned long)(g_backoffMs / 1000));
}
