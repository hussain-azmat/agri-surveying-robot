#pragma once
// ota_github: everything the rover pulls from the GitHub repo, and the one GitHub client the whole firmware shares.
//   top half  : version/manifest logic and the config-file parser. Plain C++, unit-tested on a laptop.
//   under #ifdef ARDUINO: the strict-TLS GitHub client, OTA download + flash, and config sync.
#include <stddef.h>
#include <stdint.h>
#include "config.h"

//
// Version handling for OTA. The brief: "update only if the repo version is NEWER, not
// merely different". ota/version.txt is plain text:
//
//     version=1.2.3
//     md5=0123456789abcdef0123456789abcdef
//     size=1048576
//
// (key=value, one per line, '#' comments and blank lines allowed, CRLF tolerated.)

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

//
// Runtime configuration: config/rover.cfg in the repo, fetched by the rover and cached
// in flash, so tuning needs NO rebuild. Plain "key=value" lines ('#' comments).
//
// Every value is CLAMPED to a safe range. A typo or a malicious edit of the file can
// make the rover less capable, but not unsafe (e.g. the command timeout can never be
// set above 1 s, the acceptance radius never below 3 m).




struct RtConfigReport {
  int applied = 0;     // known keys accepted (possibly after clamping)
  int clamped = 0;     // of those, values that had to be clamped
  int ignored = 0;     // unknown keys
  int malformed = 0;   // lines that were not key=value or had unparsable values
};

// Starts from `cfg` as given (normally the defaults) and applies the file on top.
// Never fails outright: bad lines are skipped and counted in the report.
RtConfigReport parseRtConfig(const char* text, RuntimeConfig* cfg);
// Re-establishes cross-field invariants (e.g. critical < low, offPath > accept).
void normalizeRtConfig(RuntimeConfig* cfg);

#ifdef ARDUINO
#include <Arduino.h>

// A small GitHub "contents" API client shared by OTA, config sync, logging and paths.
//
//  * TLS is ALWAYS validated against the embedded GitHub roots (certs.h). There is no insecure
//    fallback: if the clock is not set (certificates have validity dates) or validation fails,
//    the call fails and the caller retries later.
//  * Works for private repos (needs a fine-grained token, "Contents: read and write", limited to
//    this one repository) and for public ones (token optional for reads).
//  * Downloads go through HTTPClient::writeToStream, which handles both Content-Length and
//    chunked transfer encoding, so a file is never silently corrupted by a framing mismatch.
//  * Never blocks for longer than the connect/read timeouts, and never prints the token.
//  * Callers own retry/back-off policy; this module reports what happened precisely.

// True when WiFi is up AND the system clock is plausible (TLS needs it).
bool ghReady();
// The most recent error as plain text (no secrets). Empty if the last call succeeded.
const char* ghLastError();
// Seconds the server asked us to wait (rate limit), 0 if none. Valid right after a failed call.
uint32_t ghRetryAfterS();

// Receives downloaded bytes in order. Return false to abort the download.
typedef bool (*GhSinkFn)(const uint8_t* data, size_t n, void* ctx);

// Core download: GET a repo file and push its bytes to `sink`. `expectedSize` (> 0) is checked
// against Content-Length when the server sends one, and against the byte count afterwards.
// Returns the HTTP status (200 ok, 404 missing) or a negative error. `*received` gets the byte count.
int ghDownload(const char* repoPath, GhSinkFn sink, void* ctx, long expectedSize, size_t maxBytes, size_t* received);

// Small files (ota/version.txt): into `buf`, NUL-terminated. Fails if bigger than cap - 1.
int ghGetRaw(const char* repoPath, uint8_t* buf, size_t cap, size_t* len);

// Straight into a LittleFS file (written to fsPath + ".part", renamed on success), so big files
// (paths, config) never need to fit in RAM. Fails if it exceeds maxBytes.
int ghGetToFile(const char* repoPath, const char* fsPath, size_t maxBytes);

// Create (or, with overwrite=true, replace) a file with a commit. 201/200 = ok; 422 on a create
// means "already exists", which callers treat as success for idempotent uploads.
int ghPutFile(const char* repoPath, const uint8_t* data, size_t len, const char* commitMessage, bool overwrite);

// ---- GitHub settings and OTA bookkeeping (flash only) ----------------------------------------------
String ghOwner();
String ghRepo();
String ghToken();
bool ghTokenPresent();
void ghTokenSave(const String& token);  // fine-grained token, contents read/write, this repo only
void ghRepoSave(const String& owner, const String& repo);

void otaMarkUpdated(const char* fromVersion);  // call just before the reboot that follows a flash
bool otaJustUpdated(String* fromVersion);       // true on the first boot after an update
void otaClearJustUpdated();

// Runtime config (config/rover.cfg): load the cached copy from flash at boot ...
RuntimeConfig runtimeConfigLoad();
// ... and fetch/validate/apply a new one from the repo. True when a NEW config was applied. Only while idle.
bool runtimeConfigSync(String* message);

// Over-the-air updates from the GitHub repo.
//
//   The repo holds ota/version.txt (version, MD5 and size of the image) and ota/firmware.bin.
//   CI publishes both when FW_VERSION is bumped (see .github/workflows/build-firmware.yml).
//
//   Rules (the brief, s3):
//    * Checked at power-up and while STOPPED (disarmed), NEVER while the rover can move.
//    * Update only if the repo version is NEWER than the running one, not merely different.
//    * The image is verified against the MD5 in version.txt BEFORE it becomes the boot image.
//      Any failure (network, size, MD5, flash) aborts and the old firmware keeps running.
//    * After an update the rover reports the new version and stays DISARMED. No auto-run.
//    * Two OTA slots (partitions.csv): the running image is never overwritten.

struct OtaStatus {
  char state[16];         // idle, checking, downloading, error, uptodate
  int percent;            // 0..100 while downloading
  char message[112];
  char remoteVersion[16];
  uint32_t lastCheckAgoS; // 0xFFFFFFFF if never
  bool available;         // a newer version is in the repo (installed automatically if autoUpdate is on)
  bool autoUpdate;        // config/rover.cfg: auto_update (default on)
};

void otaBegin();
// Main loop: the first check runs as soon as the rover is online with a valid clock after power-up, then
// every ota_interval_s while stopped. With auto_update on (the default) a newer version is installed by itself.
void otaService(bool online, bool idle);
// From the control page. The caller must have verified the rover is disarmed.
//   install=false: only look, and report whether a newer version exists ("Check for update")
//   install=true : download, verify and flash it, then reboot ("Install update")
// If it returns, *message says what happened.
void otaCheckNow(String* message, bool install);
OtaStatus otaStatus();
#endif
