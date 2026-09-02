// UTC conversion without timegm().
// The ESP32 newlib has mktime() (local time) but not timegm(), and the tracker
// only ever deals in UTC: NMEA and the network clock are both UTC, and mixing a
// timezone in here would shift every timestamp in the history by whole hours.
#pragma once
#include <Arduino.h>

namespace timeutil {

// Days from 1970-01-01 for a civil date. Howard Hinnant's days_from_civil,
// valid for any year in the Gregorian calendar.
inline int32_t daysFromCivil(int32_t y, uint32_t m, uint32_t d) {
  y -= m <= 2;
  const int32_t era = (y >= 0 ? y : y - 399) / 400;
  const uint32_t yoe = static_cast<uint32_t>(y - era * 400);            // [0, 399]
  const uint32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;  // [0, 365]
  const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;           // [0, 146096]
  return era * 146097 + static_cast<int32_t>(doe) - 719468;
}

// UTC unix timestamp from calendar fields. Returns 0 for an implausible date,
// so a receiver that has not got the time yet cannot stamp records with 1980.
inline uint32_t toUnixUtc(int year, int month, int day, int hour, int minute,
                          int second) {
  if (year < 2020 || year > 2099 || month < 1 || month > 12 || day < 1 || day > 31) {
    return 0;
  }
  const int64_t days = daysFromCivil(year, static_cast<uint32_t>(month),
                                     static_cast<uint32_t>(day));
  const int64_t secs = days * 86400LL + hour * 3600LL + minute * 60LL + second;
  return (secs > 0) ? static_cast<uint32_t>(secs) : 0;
}

}  // namespace timeutil
