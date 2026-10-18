#pragma once
// PURE: no Arduino dependencies. Unit-tested on a laptop (test/test_geo_follow.cpp).
#include <stddef.h>

struct LatLon {
  double lat = 0, lon = 0;
};
struct XY {
  double x = 0, y = 0;  // metres east, metres north of the projector origin
};

double wrap180(double deg);  // -> (-180, 180]
double wrap360(double deg);  // -> [0, 360)

// Great-circle distance (Haversine) in metres.
double haversineM(const LatLon& a, const LatLon& b);
// Initial bearing from a to b, degrees clockwise from true north, [0, 360).
double bearingDeg(const LatLon& a, const LatLon& b);

// Local flat-earth projection around an origin. Error is well under a centimetre per
// kilometre at field scale, and it avoids trigonometry on every GPS update.
class Projector {
 public:
  void init(const LatLon& origin);
  XY toXY(const LatLon& p) const;
  LatLon toLatLon(const XY& p) const;
  const LatLon& origin() const { return o_; }

 private:
  LatLon o_;
  double mPerDegLat_ = 111320.0, mPerDegLon_ = 111320.0;
};

double distXY(const XY& a, const XY& b);
// Distance from p to the segment a-b (clamped to the endpoints).
double distPointSegment(const XY& p, const XY& a, const XY& b);

bool latLonPlausible(double lat, double lon);

// Douglas-Peucker simplification. Keeps the first and last points. Writes at most `cap`
// points to `out` and returns the count. Iterative (no recursion), O(n log n) typical.
size_t simplifyPath(const LatLon* in, size_t n, double epsM, LatLon* out, size_t cap);
