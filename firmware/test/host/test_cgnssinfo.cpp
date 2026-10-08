// Host-side check of the +CGNSSINFO parser, run in CI:
//   g++ -std=c++17 -Wall -I src test/host/test_cgnssinfo.cpp -o /tmp/t && /tmp/t
// The parser was written before any real fix had been seen. Published A76xx
// lines carry a trailing "satellites used" field after VDOP, which the first
// version rejected, so every real fix came back as "no fix" (2026-10-08).
#include <cmath>
#include <cstdio>

#include "modem/cgnssinfo.h"

namespace {

int failed = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failed;
  }
}

bool near(double a, double b, double eps) { return std::fabs(a - b) < eps; }

}  // namespace

int main() {
  cgnss::Fix f;

  // SIMCom A76xx manual example: four satellite counts, ddmm.mmmmmm, trailing
  // satellites-used field.
  check(cgnss::parse(" 2,09,05,00,00,3113.330650,N,12121.262554,E,131117,091918.00,"
                     "32.9,0.0,255.0,1.1,0.8,0.7,14",
                     f),
        "manual example accepted");
  check(near(f.lat, 31.0 + 13.330650 / 60.0, 1e-6), "manual example latitude (ddmm)");
  check(near(f.lon, 121.0 + 21.262554 / 60.0, 1e-6), "manual example longitude (ddmm)");
  check(near(f.hdop, 0.8f, 1e-4), "manual example HDOP is the field before VDOP");
  check(f.sats == 14, "manual example uses the satellites-used field");
  // 2017 is outside the 2020-2060 window of timeutil::toUnixUtc, so no time.
  check(f.utc_ts == 0, "manual example: 2017 date rejected as implausible");

  // Real fix with empty GLONASS/GALILEO/BEIDOU counts, decimal degrees,
  // empty course.
  check(cgnss::parse("3,08,,,,22.5711975,N,113.8874359,E,010625,084112.00,20.9,0.000,,"
                     "2.08,1.15,1.74,13",
                     f),
        "decimal-degree example accepted");
  check(near(f.lat, 22.5711975, 1e-7), "decimal latitude kept as is");
  check(near(f.lon, 113.8874359, 1e-7), "decimal longitude kept as is");
  check(near(f.hdop, 1.15f, 1e-4), "decimal example HDOP");
  check(f.sats == 13, "decimal example satellites used");
  check(f.utc_ts == 1748767272u, "decimal example time 2025-06-01 08:41:12 UTC");

  // Older layout without the trailing field must keep working.
  check(cgnss::parse("2,09,05,00,3113.330650,N,12121.262554,E,131117,091918.0,"
                     "32.9,0.0,255.0,1.1,0.8,0.7",
                     f),
        "three counts, no trailing field");
  check(near(f.hdop, 0.8f, 1e-4), "three counts HDOP");
  check(f.sats == 14, "three counts: sum of the per-system counts");

  // Southern and western hemispheres.
  check(cgnss::parse("3,10,,,,33.8688,S,151.2093,W,081026,120000.00,5.0,10.0,90.0,"
                     "1.5,0.9,1.2,10",
                     f),
        "south/west accepted");
  check(f.lat < 0 && f.lon < 0, "south/west signs");
  check(near(f.speed_kmh, 18.52f, 1e-3), "speed treated as knots");

  // No fix: what the modem prints while searching.
  check(!cgnss::parse(",,,,,,,,,,,,,,,", f), "empty line rejected");
  check(!cgnss::parse(" ,,,,,,,,,,,,,,,,", f), "empty line with space rejected");
  check(!cgnss::parse("1,03,,,,,,,,,,,,,,,", f), "mode 1 rejected");
  check(!cgnss::parse("3,08,,,,,N,,E,010625,084112.00,20.9,0.000,,2.08,1.15,1.74,13", f),
        "missing coordinates rejected");

  std::printf("%s\n", failed ? "FAILED" : "ok");
  return failed ? 1 : 0;
}
