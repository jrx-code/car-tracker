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
// Published fix lines (SIMCom manual, field reports) end with a "satellites
// used" count after VDOP; test/host/test_cgnssinfo.cpp holds them.
// [TO VERIFY with a real fix from our A7670: lat format and the speed unit.]
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

  // The layout is found from the latitude hemisphere instead of from the total
  // field count: the number of per-system satellite counts varies (3 or 4) and
  // newer firmware appends "satellites used" after VDOP. The first parser
  // required exactly 16 or 17 fields and so rejected every real fix line,
  // which has 18 (2026-10-08). From the N/S field k the order is fixed:
  // lat k-1, N/S k, lon k+1, E/W k+2, date k+3, time k+4, alt k+5,
  // speed k+6, course k+7, PDOP k+8, HDOP k+9, VDOP k+10, [used k+11].
  int k = -1;
  for (int i = 2; i < n; i++) {
    if ((f[i][0] == 'N' || f[i][0] == 'S') && f[i][1] == '\0') {
      k = i;
      break;
    }
  }
  if (k < 0 || n < k + 11) return false;
  const int svs = k - 2;  // fields 1..k-2 are the per-system counts
  if (svs < 3 || svs > 4) return false;
  if (!f[k - 1][0] || !f[k + 1][0]) return false;
  if (f[k + 2][0] != 'E' && f[k + 2][0] != 'W') return false;

  int sats = 0;
  if (n > k + 11 && f[k + 11][0]) {
    sats = atoi(f[k + 11]);
  } else {
    for (int i = 1; i <= svs; i++) sats += atoi(f[i]);
  }

  double lat = atof(f[k - 1]);
  double lon = atof(f[k + 1]);
  // A latitude of 100 or more can only be ddmm; decimal degrees stop at 90.
  // Below 1 degree the two formats coincide in magnitude, irrelevant here.
  if (std::fabs(lat) >= 100.0) {
    lat = ddmmToDeg(lat);
    lon = ddmmToDeg(lon);
  }
  if (f[k][0] == 'S') lat = -lat;
  if (f[k + 2][0] == 'W') lon = -lon;

  const char* date = f[k + 3];  // ddmmyy
  const char* tim = f[k + 4];   // hhmmss.s
  uint32_t ts = 0;
  if (strlen(date) >= 6 && strlen(tim) >= 6) {
    auto two = [](const char* s) { return (s[0] - '0') * 10 + (s[1] - '0'); };
    ts = timeutil::toUnixUtc(2000 + two(date + 4), two(date + 2), two(date),
                             two(tim), two(tim + 2), two(tim + 4));
  }

  out.lat = lat;
  out.lon = lon;
  out.alt = static_cast<float>(atof(f[k + 5]));
  out.speed_kmh = static_cast<float>(atof(f[k + 6]) * 1.852);
  out.course = static_cast<float>(atof(f[k + 7]));
  out.hdop = f[k + 9][0] ? static_cast<float>(atof(f[k + 9])) : 99.0f;
  out.sats = sats;
  out.utc_ts = ts;
  return true;
}

}  // namespace cgnss
