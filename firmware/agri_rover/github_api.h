#pragma once
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
#include <Arduino.h>

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
