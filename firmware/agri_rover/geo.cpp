// PURE: no Arduino dependencies.
#include "geo.h"

#include <math.h>
#include <vector>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthR = 6371008.8;  // mean radius, metres
inline double rad(double d) { return d * kPi / 180.0; }
inline double deg(double r) { return r * 180.0 / kPi; }
}  // namespace

double wrap180(double d) {
  if (!isfinite(d)) return 0.0;
  d = fmod(d, 360.0);
  if (d > 180.0) d -= 360.0;
  if (d <= -180.0) d += 360.0;
  return d;
}

double wrap360(double d) {
  if (!isfinite(d)) return 0.0;
  d = fmod(d, 360.0);
  if (d < 0) d += 360.0;
  return d;
}

double haversineM(const LatLon& a, const LatLon& b) {
  const double p1 = rad(a.lat), p2 = rad(b.lat);
  const double dp = p2 - p1, dl = rad(b.lon - a.lon);
  const double h = sin(dp / 2) * sin(dp / 2) + cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2);
  return 2.0 * kEarthR * asin(fmin(1.0, sqrt(h)));
}

double bearingDeg(const LatLon& a, const LatLon& b) {
  const double p1 = rad(a.lat), p2 = rad(b.lat), dl = rad(b.lon - a.lon);
  const double y = sin(dl) * cos(p2);
  const double x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl);
  return wrap360(deg(atan2(y, x)));
}

void Projector::init(const LatLon& origin) {
  o_ = origin;
  mPerDegLat_ = kEarthR * kPi / 180.0;
  mPerDegLon_ = mPerDegLat_ * cos(rad(origin.lat));
}

XY Projector::toXY(const LatLon& p) const {
  XY r;
  r.x = (p.lon - o_.lon) * mPerDegLon_;
  r.y = (p.lat - o_.lat) * mPerDegLat_;
  return r;
}

LatLon Projector::toLatLon(const XY& p) const {
  LatLon r;
  r.lat = o_.lat + p.y / mPerDegLat_;
  r.lon = o_.lon + (mPerDegLon_ > 1e-9 ? p.x / mPerDegLon_ : 0.0);
  return r;
}

double distXY(const XY& a, const XY& b) { return hypot(a.x - b.x, a.y - b.y); }

double distPointSegment(const XY& p, const XY& a, const XY& b) {
  const double dx = b.x - a.x, dy = b.y - a.y;
  const double len2 = dx * dx + dy * dy;
  if (len2 < 1e-12) return distXY(p, a);
  double t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2;
  t = fmax(0.0, fmin(1.0, t));
  return distXY(p, XY{a.x + t * dx, a.y + t * dy});
}

bool latLonPlausible(double lat, double lon) {
  return isfinite(lat) && isfinite(lon) && lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0 &&
         !(lat == 0.0 && lon == 0.0);  // (0,0) is what an uninitialised receiver reports
}

size_t simplifyPath(const LatLon* in, size_t n, double epsM, LatLon* out, size_t cap) {
  if (!in || !out || cap == 0 || n == 0) return 0;
  if (n <= 2) {
    size_t c = 0;
    for (size_t i = 0; i < n && c < cap; i++) out[c++] = in[i];
    return c;
  }
  Projector pr;
  pr.init(in[0]);
  std::vector<XY> xy(n);
  for (size_t i = 0; i < n; i++) xy[i] = pr.toXY(in[i]);

  std::vector<unsigned char> keep(n, 0);
  keep[0] = keep[n - 1] = 1;
  std::vector<std::pair<size_t, size_t>> stack;
  stack.push_back({0, n - 1});
  while (!stack.empty()) {
    auto [lo, hi] = stack.back();
    stack.pop_back();
    if (hi <= lo + 1) continue;
    double best = -1.0;
    size_t bestI = lo;
    for (size_t i = lo + 1; i < hi; i++) {
      const double d = distPointSegment(xy[i], xy[lo], xy[hi]);
      if (d > best) {
        best = d;
        bestI = i;
      }
    }
    if (best > epsM) {
      keep[bestI] = 1;
      stack.push_back({lo, bestI});
      stack.push_back({bestI, hi});
    }
  }
  size_t c = 0;
  for (size_t i = 0; i < n && c < cap; i++) {
    if (keep[i]) out[c++] = in[i];
  }
  return c;
}
