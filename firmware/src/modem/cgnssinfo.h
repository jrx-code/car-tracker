// Parser for the SIMCom A76xx "+CGNSSINFO:" response. Pure function, no
// Arduino dependency, so it compiles on the host for tests.
//
// TinyGSM 0.12 has no GNSS support for the A7672X class at all, and the
// response layout moves between modem firmware releases:
//   - some list three satellite counts (GPS, GLONASS, BEIDOU), newer ones add
//     GALILEO as a fourth;
//   - some print lat/lon as ddmm.mmmmmm, newer ones as decimal degrees.
// Both are detected from the line itself instead of being assumed. What is
// taken as fixed: the fields after the satellite counts are lat, N/S, lon,
// E/W, date ddmmyy, time hhmmss.s, alt, speed, course, and the line ends with
// PDOP, HDOP, VDOP. Speed is treated as knots, as in the SIMCom manual.
// [TO VERIFY with a real fix on A7670M7_B09V01_250619: field count, lat format
//  and the speed unit. As of 2026-09-28 only the no-fix line was observed.]
#pragma once

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "util/timeutil.h"

namespace cgnss {

struct Fix {
  double lat = 0, lon = 0;
  float speed_kmh = 0, course = 0, alt = 0, hdop = 99;
  int sats = 0;
  uint32_t utc_ts = 0;
};

// ddmm.mmmm -> degrees. Only called when the value is known to be ddmm.
inline double ddmmToDeg(double v) {
  const double deg = std::floor(v / 100.0);
  return deg + (v - deg * 100.0) / 60.0;
}

// line: everything after "+CGNSSINFO:", with or without the leading space.
// Returns false for "no fix" (empty mode field, or mode below 2).
inline bool parse(const char* line, Fix& out) {
  constexpr int kMaxFields = 24;
  char buf[160];
  strncpy(buf, line, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  // Split on commas without strtok: empty fields must be kept.
  const char* f[kMaxFields];
  int n = 0;
  char* p = buf;
  while (*p == ' ') p++;
  f[n++] = p;
  for (; *p && n < kMaxFields; p++) {
    if (*p == ',') {
      *p = '\0';
      f[n++] = p + 1;
    } else if (*p == '\r' || *p == '\n') {
      *p = '\0';
      break;
    }
  }

  const int mode = f[0][0] ? atoi(f[0]) : 0;
  if (mode < 2) return false;

  // 1 mode + S satellite counts + 9 position fields + 3 DOP fields, with S
  // being 3 or 4. Anything else is a layout this parser does not know.
  int svs;
  if (n == 1 + 3 + 9 + 3) {
    svs = 3;
  } else if (n == 1 + 4 + 9 + 3) {
    svs = 4;
  } else {
    return false;
  }

  int sats = 0;
  for (int i = 1; i <= svs; i++) sats += atoi(f[i]);

  const int b = 1 + svs;
  if (!f[b][0] || !f[b + 2][0]) return false;
  double lat = atof(f[b]);
  double lon = atof(f[b + 2]);
  // A latitude of 100 or more can only be ddmm; decimal degrees stop at 90.
  // Below 1 degree the two formats coincide in magnitude, irrelevant here.
  if (std::fabs(lat) >= 100.0) {
    lat = ddmmToDeg(lat);
    lon = ddmmToDeg(lon);
  }
  if (f[b + 1][0] == 'S') lat = -lat;
  if (f[b + 3][0] == 'W') lon = -lon;

  const char* date = f[b + 4];  // ddmmyy
  const char* tim = f[b + 5];   // hhmmss.s
  uint32_t ts = 0;
  if (strlen(date) >= 6 && strlen(tim) >= 6) {
    auto two = [](const char* s) { return (s[0] - '0') * 10 + (s[1] - '0'); };
    ts = timeutil::toUnixUtc(2000 + two(date + 4), two(date + 2), two(date),
                             two(tim), two(tim + 2), two(tim + 4));
  }

  out.lat = lat;
  out.lon = lon;
  out.alt = static_cast<float>(atof(f[b + 6]));
  out.speed_kmh = static_cast<float>(atof(f[b + 7]) * 1.852);
  out.course = static_cast<float>(atof(f[b + 8]));
  out.hdop = f[n - 2][0] ? static_cast<float>(atof(f[n - 2])) : 99.0f;
  out.sats = sats;
  out.utc_ts = ts;
  return true;
}

}  // namespace cgnss
