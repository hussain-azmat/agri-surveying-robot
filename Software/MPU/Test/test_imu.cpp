// Host unit tests: MPU (IMU averaging between log rows, and the straight-line drift monitor).
#include <math.h>
#include "imu.h"
#include "mini_test.h"

TEST(imu_window_averages_and_tracks_the_peak) {
  ImuWindow w;
  ImuData d;
  d.ok = true;
  for (int i = 0; i < 100; i++) {  // a vibration: the gyro swings +-0.2 rad/s around 0.01
    d.accelX = (i % 2) ? 1.0f : -1.0f; d.accelY = 0; d.accelZ = 9.8f;
    d.gyroX = 0.01f + ((i % 2) ? 0.2f : -0.2f); d.gyroY = 0; d.gyroZ = 0;
    w.add(d);
  }
  const ImuMeans m = w.take();
  CHECK(m.n == 100);
  CHECK(fabsf(m.ax) < 1e-4f && fabsf(m.az - 9.8f) < 1e-4f);
  CHECK(fabsf(m.gx - 0.01f) < 1e-4f);
  CHECK(fabsf(m.gyroPeak - 0.21f) < 1e-4f);  // the single readings were far from the mean: the peak keeps that visible
  CHECK(w.take().n == 0);                    // take() starts a new window
}

TEST(imu_window_ignores_a_dead_sensor) {
  ImuWindow w;
  ImuData d;  // ok = false
  d.accelZ = 123.0f;
  for (int i = 0; i < 10; i++) w.add(d);
  const ImuMeans m = w.take();
  CHECK(m.n == 0 && m.az == 0.0f);  // nothing is invented: the row will show empty IMU fields
}

namespace {
void run(StraightMonitor& m, float seconds, float rateDps, float thr, float turn, bool ok = true) {
  for (float t = 0; t < seconds; t += 0.01f) m.update(0.01f, rateDps, thr, turn, ok);
}
}  // namespace

TEST(straight_monitor_measures_the_exact_veer_after_the_settling_time) {
  StraightMonitor m;
  run(m, 0.4f, 0.0f, 0.6f, 0.0f);
  CHECK(!m.valid());                       // not even a second yet
  run(m, 5.0f, 2.0f, 0.6f, 0.0f);          // the rover veers right at a steady 2 deg/s
  CHECK(m.valid());
  CHECK(fabsf(m.driftDeg() - 2.0f * (5.4f - StraightMonitor::SETTLE_S)) < 0.1f);  // about 9.8 deg
  CHECK(m.straightS() > 5.3f);
}

TEST(straight_monitor_ignores_turns_the_operator_asked_for) {
  StraightMonitor m;
  run(m, 3.0f, 0.0f, 0.6f, 0.0f);
  CHECK(m.valid());
  run(m, 0.1f, 40.0f, 0.6f, 0.5f);         // a button turns the rover: the run ends
  CHECK(!m.valid() && m.driftDeg() == 0.0f);
  run(m, 0.3f, 25.0f, 0.6f, 0.0f);         // the button is released, the rover is still swinging round...
  run(m, 2.0f, 0.0f, 0.6f, 0.0f);          // ...and then goes straight
  CHECK(m.valid());
  CHECK(fabsf(m.driftDeg()) < 0.05f);      // the swing during the settling time is not counted
}

TEST(straight_monitor_reports_a_veer_to_the_left_as_negative) {
  StraightMonitor m;
  run(m, 4.0f, -1.0f, -0.5f, 0.0f);        // reversing in a straight line, drifting left
  CHECK(m.valid() && m.driftDeg() < -2.5f);
}

TEST(straight_monitor_needs_motion_a_working_imu_and_sane_numbers) {
  StraightMonitor m;
  run(m, 3.0f, 5.0f, 0.0f, 0.0f);          // standing still: gyro noise is not drift
  CHECK(!m.valid());
  run(m, 3.0f, 5.0f, 0.6f, 0.0f, false);   // IMU not answering
  CHECK(!m.valid());
  run(m, 3.0f, NAN, 0.6f, 0.0f);           // garbage rate
  CHECK(!m.valid());
  run(m, 3.0f, 1.0f, 0.6f, 0.0f);
  CHECK(m.valid() && fabsf(m.driftDeg() - 2.5f) < 0.2f);
  m.update(0.01f, 1.0f, 0.0f, 0.0f, true);  // the rover stops
  CHECK(!m.valid() && m.straightS() == 0.0f);
}
