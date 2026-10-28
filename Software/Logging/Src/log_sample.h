#pragma once
// One log row's worth of data, and the queue that carries it from the control task to the flash writer.
// Plain C++ (no Arduino): unit-tested on a laptop.
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "gps.h"
#include "imu.h"

enum class LogMode : uint8_t { OFF = 0, MAN = 1, REC = 2, AUTO = 3 };

struct LogSample {
  int64_t sec = 0;        // system clock when the sample was taken; 0 = the clock was not set yet
  int32_t usec = 0;
  uint32_t uptimeMs = 0;  // millis() when it was taken
  GpsData gps;            // position, fix counter and age, speed, course, GPS time
  ImuData imu;            // instantaneous: yaw, pitch, roll
  ImuMeans win;           // raw accel / gyro AVERAGED over the time since the previous row
  bool driftValid = false;   // a straight run of at least a second is in progress
  float driftDeg = 0;        // veer from a straight line during that run, + = clockwise (right)
  float straightS = 0;       // how long that run has lasted
  LogMode mode = LogMode::OFF;
  float throttle = 0, turn = 0;  // the commands applied to the motors
  bool battKnown = false;
  float battV = 0;
  float xteM = 0;         // cross-track error: written only in AUTO
  int wp = 0;
  char event[24] = "";    // e.g. "ARM:AUTO", "DISARM:LINK_LOST", "GAP:12"
};

// Fixed-size FIFO between the control task (the producer, at the GPS rate) and the flash writer in the main loop.
// Not thread-safe by itself: the caller holds a lock around push() and pop(). When it is full the OLDEST sample is
// dropped and counted, so the writer can put a visible "GAP:n" row in the data instead of silently skipping time.
template <size_t N>
class SampleRing {
 public:
  void push(const LogSample& s) {
    if (n_ == N) {
      head_ = (head_ + 1) % N;
      n_--;
      dropped_++;
    }
    buf_[(head_ + n_) % N] = s;
    n_++;
  }
  bool pop(LogSample* out) {
    if (!n_) return false;
    *out = buf_[head_];
    head_ = (head_ + 1) % N;
    n_--;
    return true;
  }
  size_t size() const { return n_; }
  uint32_t takeDropped() {
    const uint32_t d = dropped_;
    dropped_ = 0;
    return d;
  }

 private:
  LogSample buf_[N];
  size_t head_ = 0, n_ = 0;
  uint32_t dropped_ = 0;
};
