#pragma once
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
#include <Arduino.h>

struct OtaStatus {
  char state[16];         // idle, checking, downloading, error, uptodate
  int percent;            // 0..100 while downloading
  char message[112];
  char remoteVersion[16];
  uint32_t lastCheckAgoS; // 0xFFFFFFFF if never
};

void otaBegin();
// Main loop: runs the scheduled checks (first one shortly after the rover comes online).
void otaService(bool online, bool idle);
// Manual check from the control page. The caller must have verified the rover is disarmed.
// May flash and reboot; if it returns, *message says what happened.
void otaCheckNow(String* message);
OtaStatus otaStatus();
