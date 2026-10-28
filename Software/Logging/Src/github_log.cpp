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
  return "utc,uptime_ms,fix_n,fix_age_ms,gps_time,lat,lon,alt_m,fix,sats,hdop,speed_mps,course_deg,yaw_deg,pitch_deg,roll_deg,"
         "accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z,gyro_peak,drift_deg,straight_s,mode,throttle,turn,batt_v,xte_m,wp,event\n";
}

size_t formatLogPreamble(char* out, size_t cap, const char* fw, const char* rover, const RuntimeConfig& c) {
  char r[40];  // the rover id comes from the chip, but never let a stray newline split the metadata line
  size_t k = 0;
  for (const char* s = rover ? rover : ""; *s && k < sizeof(r) - 1; s++) r[k++] = (*s == '\n' || *s == '\r' || *s == ',') ? '_' : *s;
  r[k] = '\0';
  const int n = snprintf(out, cap,
                         "# schema=%d fw=%s rover=%s log_every=%u log_always=%u max_speed=%.2f auto_speed=%.2f accept_radius_m=%.1f turn_full_dps=%.0f\n",
                         LOG_SCHEMA, fw ? fw : "", r, (unsigned)c.logEvery, c.logAlways ? 1u : 0u, fin(c.maxSpeed), fin(c.autoSpeed),
                         fin(c.acceptRadiusM), fin(c.turnFullDps));
  return finish(out, cap, n);
}

size_t formatUtc(char* out, size_t cap, int64_t sec, int32_t usec, char* dateOut) {
  if (cap) out[0] = '\0';
  if (dateOut) strcpy(dateOut, "00000000");
  if (sec < 1704067200 || !cap) return 0;  // clock not set (before 2024-01-01)
  // Civil-from-days (Howard Hinnant): no time.h, so it behaves the same on the ESP32 and on a laptop.
  const int64_t z = sec / 86400 + 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = (unsigned)(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t y = (int64_t)yoe + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned d = doy - (153 * mp + 2) / 5 + 1;
  const unsigned m = mp < 10 ? mp + 3 : mp - 9;
  const unsigned yy = (unsigned)((m <= 2 ? y + 1 : y) % 10000);
  const unsigned rem = (unsigned)(sec % 86400);
  const unsigned ms = (unsigned)((usec < 0 ? 0 : usec) / 1000) % 1000;
  const int n = snprintf(out, cap, "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", yy, m % 100, d % 100, (rem / 3600) % 100,
                         (rem % 3600) / 60, rem % 60, ms);
  if (dateOut) snprintf(dateOut, 9, "%04u%02u%02u", yy, m % 100, d % 100);
  return finish(out, cap, n);
}

size_t logHeaderBlockLen(const char* text, size_t n) {
  size_t i = 0;
  while (i < n) {
    size_t e = i;
    while (e < n && text[e] != '\n') e++;
    if (e >= n) return 0;  // the line is not complete yet
    const bool comment = text[i] == '#';
    i = e + 1;
    if (!comment) return i;  // the header line: the block ends here
  }
  return 0;
}

size_t formatLogRow(char* out, size_t cap, const LogSample& s) {
  char utc[40];
  formatUtc(utc, sizeof(utc), s.sec, s.usec, nullptr);
  const GpsData& g = s.gps;
  const ImuData& i = s.imu;
  const ImuMeans& w = s.win;
  char ageS[12] = "", gpsT[12] = "", lat[20] = "", lon[20] = "", alt[12] = "", crs[8] = "";
  if (g.fixAgeMs != 0xFFFFFFFFu) snprintf(ageS, sizeof(ageS), "%lu", (unsigned long)g.fixAgeMs);
  if (g.timeValid) snprintf(gpsT, sizeof(gpsT), "%02u:%02u:%02u", (unsigned)g.hour % 100, (unsigned)g.minute % 100, (unsigned)g.second % 100);
  if (g.fix) {
    snprintf(lat, sizeof(lat), "%.7f", fin(g.lat));
    snprintf(lon, sizeof(lon), "%.7f", fin(g.lon));
    snprintf(alt, sizeof(alt), "%.1f", fin(g.altM));
  }
  if (g.courseValid) snprintf(crs, sizeof(crs), "%.0f", fin(g.courseDeg));
  char yaw[12] = "", pitch[12] = "", roll[12] = "";
  if (i.ok) {
    snprintf(pitch, sizeof(pitch), "%.1f", fin(i.pitchDeg));
    snprintf(roll, sizeof(roll), "%.1f", fin(i.rollDeg));
    if (i.yawValid) snprintf(yaw, sizeof(yaw), "%.1f", fin(i.yawDeg));
  }
  char imu[96];
  if (w.n) {
    snprintf(imu, sizeof(imu), "%.2f,%.2f,%.2f,%.3f,%.3f,%.3f,%.3f", fin(w.ax), fin(w.ay), fin(w.az), fin(w.gx), fin(w.gy),
             fin(w.gz), fin(w.gyroPeak));
  } else {
    strcpy(imu, ",,,,,,");  // seven empty fields: the IMU gave no readings in this window
  }
  char drift[12] = "", straight[12] = "";
  if (s.driftValid) {
    snprintf(drift, sizeof(drift), "%.2f", fin(s.driftDeg));
    snprintf(straight, sizeof(straight), "%.1f", fin(s.straightS));
  }
  static const char* const kMode[] = {"OFF", "MAN", "REC", "AUTO"};
  const char* mode = kMode[(unsigned)s.mode < 4 ? (unsigned)s.mode : 0];
  char batt[12] = "", xte[12] = "", wp[12] = "";
  if (s.battKnown) snprintf(batt, sizeof(batt), "%.2f", fin(s.battV));
  if (s.mode == LogMode::AUTO) {
    snprintf(xte, sizeof(xte), "%.2f", fin(s.xteM));
    snprintf(wp, sizeof(wp), "%d", s.wp);
  }
  char ev[sizeof(s.event)];
  size_t k = 0;
  for (const char* e = s.event; *e && k < sizeof(ev) - 1; e++) ev[k++] = (*e == ',' || *e == '\n' || *e == '\r') ? '_' : *e;
  ev[k] = '\0';
  const int n = snprintf(out, cap, "%s,%lu,%lu,%s,%s,%s,%s,%s,%u,%u,%.1f,%.2f,%s,%s,%s,%s,%s,%s,%s,%s,%.2f,%.2f,%s,%s,%s,%s\n", utc,
                         (unsigned long)s.uptimeMs, (unsigned long)g.fixCount, ageS, gpsT, lat, lon, alt, (unsigned)g.quality,
                         (unsigned)g.sats, fin(g.hdop), fin(g.speedMps), crs, yaw, pitch, roll, imu, drift, straight, mode,
                         fin(s.throttle), fin(s.turn), batt, xte, wp, ev);
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


char g_buf[3072];
size_t g_bufN = 0;
File g_cur;
bool g_curOpen = false;
char g_curDate[9] = "00000000";  // YYYYMMDD of the first row in cur.csv, "00000000" if the clock was not set
uint32_t g_rows = 0;
bool g_full = false;
char g_err[96] = "";
uint32_t g_lastUploadMs = 0;
uint32_t g_backoffMs = 0, g_nextTryMs = 0;
uint32_t g_seq = 0;
bool g_began = false;
RuntimeConfig g_cfg;  // the last config the main loop saw: written into each file's metadata line

// ---- samples queued by the control task --------------------------------------------------------------------
SampleRing<LOG_RING_SAMPLES> g_ring;
portMUX_TYPE g_ringMux = portMUX_INITIALIZER_UNLOCKED;
uint32_t g_droppedTotal = 0;

// ---- rate / space estimate -------------------------------------------------------------------------------------
float g_rowBytesAvg = 170.0f;
uint32_t g_rateStartMs = 0, g_rateRows = 0;
float g_rowsPerS = 0;

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
  if (g_cur.size() == 0) {  // a new file starts with its metadata line and its header
    char pre[320];
    const size_t n = formatLogPreamble(pre, sizeof(pre), FW_VERSION, roverId().c_str(), g_cfg);
    if (n) g_cur.write((const uint8_t*)pre, n);
    g_cur.print(logHeader());
  }
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

// Size of the metadata lines + header line at the start of a file (0 if the file has no complete header yet).
size_t headerBlock(File& f, char* buf, size_t cap) {
  f.seek(0);
  const size_t got = f.read((uint8_t*)buf, cap);
  return logHeaderBlockLen(buf, got);
}

// Moves cur.csv into the upload queue. Returns true if something was queued.
bool rotate() {
  closeCur();
  File f = LittleFS.open("/log/cur.csv", "r");
  if (!f) return false;
  const size_t size = f.size();
  char probe[768];
  const size_t hb = headerBlock(f, probe, sizeof(probe));
  f.close();
  if (size <= hb) {  // header only: nothing worth uploading
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

void appendSample(const LogSample& s, bool spaceOk) {
  if (!spaceOk) {
    g_full = true;
    return;
  }
  g_full = false;
  char date[9];
  char utc[40];
  formatUtc(utc, sizeof(utc), s.sec, s.usec, date);
  if (strcmp(g_curDate, "00000000") == 0 && strcmp(date, "00000000") != 0) strcpy(g_curDate, date);
  char line[420];
  const size_t n = formatLogRow(line, sizeof(line), s);
  if (n == 0) return;
  if (g_bufN + n > sizeof(g_buf)) flushBuf();
  memcpy(g_buf + g_bufN, line, n);
  g_bufN += n;
  g_rows++;
  g_rowBytesAvg += 0.05f * ((float)n - g_rowBytesAvg);
  g_rateRows++;
}

bool ringPop(LogSample* out, uint32_t* dropped) {
  portENTER_CRITICAL(&g_ringMux);
  const bool ok = g_ring.pop(out);
  *dropped = ok ? g_ring.takeDropped() : 0;
  portEXIT_CRITICAL(&g_ringMux);
  return ok;
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

LogSample sampleFromState(const RoverState& s) {
  LogSample x;
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  x.sec = tv.tv_sec;
  x.usec = (int32_t)tv.tv_usec;
  x.uptimeMs = millis();
  x.gps = s.gps;
  x.imu = s.imu;
  x.mode = s.mode == Mode::AUTO ? LogMode::AUTO : (s.mode == Mode::MANUAL ? (s.recording ? LogMode::REC : LogMode::MAN) : LogMode::OFF);
  x.throttle = s.cmdThrottle;
  x.turn = s.cmdTurn;
  x.battKnown = s.battKnown;
  x.battV = s.battV;
  x.xteM = s.follow.xteM;
  x.wp = s.follow.wpIndex;
  return x;
}

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
    String first, line;
    bool headerSeen = false;
    for (int k = 0; k < 8 && f.available(); k++) {  // skip the "# ..." lines and the header, take the first data row
      line = f.readStringUntil('\n');
      if (line.length() && line[0] == '#') continue;
      if (!headerSeen) {
        headerSeen = true;
        continue;
      }
      first = line;
      break;
    }
    f.close();
    if (first.length() >= 10 && first[4] == '-' && first[7] == '-') {
      snprintf(g_curDate, sizeof(g_curDate), "%.4s%.2s%.2s", first.c_str(), first.c_str() + 5, first.c_str() + 8);
    }
    rotate();
  }
}

void logPush(const LogSample& s) {
  portENTER_CRITICAL(&g_ringMux);
  g_ring.push(s);
  portEXIT_CRITICAL(&g_ringMux);
}

void logTick(const RoverState& s) {
  if (!g_began) return;
  g_cfg = s.cfg;
  const uint32_t now = millis();
  // Write what the control task queued. A few dozen rows per call at most, so the main loop stays responsive.
  LogSample x;
  uint32_t dropped = 0;
  static uint32_t lastSpaceCheck = 0;
  static bool spaceOk = true;
  if (now - lastSpaceCheck > 2000) {  // free space does not change fast; do not ask the filesystem for every row
    lastSpaceCheck = now;
    spaceOk = freeBytes() >= LOG_FS_RESERVE_BYTES;
  }
  for (int guard = 0; guard < 32 && ringPop(&x, &dropped); guard++) {
    if (dropped) {  // the writer fell behind: say so in the data
      g_droppedTotal += dropped;
      LogSample gap = x;
      gap.win = ImuMeans();
      gap.driftValid = false;
      snprintf(gap.event, sizeof(gap.event), "GAP:%lu", (unsigned long)dropped);
      appendSample(gap, spaceOk);
    }
    appendSample(x, spaceOk);
  }
  // Write to flash about once a second; the buffer holds ~18 rows, so a power cut loses about a second.
  static uint32_t lastFlush = 0;
  if (now - lastFlush > 1000) {
    lastFlush = now;
    flushBuf();
  }
  // rows per second over the last 10 s, for the "minutes of flash left" estimate
  if (now - g_rateStartMs >= 10000) {
    g_rowsPerS = g_rateRows * 1000.0f / (float)(now - g_rateStartMs);
    g_rateStartMs = now;
    g_rateRows = 0;
  }
}

void logEvent(const RoverState& s, const char* event) {
  if (!g_began || !s.cfg.loggingEnabled) return;
  LogSample x = sampleFromState(s);
  strncpy(x.event, event ? event : "", sizeof(x.event) - 1);
  logPush(x);
}

void logFlush() {
  // A final drain, so a row queued just before a disarm is not left behind in RAM.
  LogSample x;
  uint32_t dropped = 0;
  const bool spaceOk = freeBytes() >= LOG_FS_RESERVE_BYTES;
  for (int guard = 0; guard < 64 && ringPop(&x, &dropped); guard++) appendSample(x, spaceOk);
  flushBuf();
}

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
  st.dropped = g_droppedTotal;
  st.full = g_full;
  const uint32_t room = freeBytes();
  const float bps = g_rowsPerS * g_rowBytesAvg;  // bytes per second being written right now
  st.minutesLeft = (bps > 1.0f && room > LOG_FS_RESERVE_BYTES) ? (int32_t)((room - LOG_FS_RESERVE_BYTES) / bps / 60.0f) : -1;
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
  // Every chunk after the first carries the file's OWN metadata and header lines, so a file written by an older
  // firmware (other columns) is never given the new header, and every chunk stands alone.
  char hdr[768];
  size_t hdrLen = headerBlock(f, hdr, sizeof(hdr));
  if (hdrLen == 0) {  // no complete header found: fall back to the current one
    hdrLen = strlen(logHeader());
    memcpy(hdr, logHeader(), hdrLen);
  }
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
