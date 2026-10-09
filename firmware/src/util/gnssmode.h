// Trip start/end from GNSS alone, plain C++ on purpose (no Arduino headers), so
// test/host/ can compile it on the CI runner.
//
// Used when there is neither a voltage divider nor an accelerometer: the PoC
// board on a power bank. Without this the mode never left PARKED, so no track
// could ever be recorded and PoC criterion 1 was out of reach (review
// 2026-10-08). Power is no concern on a bank, so the receiver stays on and GNSS
// decides.
//
// Speed alone failed on the bench (2026-10-08): a board lying still reported
// 0 -> 6.9 -> 3.9 km/h with HDOP ~2, opened a trip and never closed it, since
// the jitter kept resetting a speed-based stop timer. Displacement filters
// that: parked-to-driving needs the position to leave the parking spot,
// driving-to-parked needs it to stay inside a small circle.
#pragma once

#include <cmath>
#include <cstdint>

namespace gnssmode {

// [DO ZMIERZENIA] thresholds: set from the first drive, not from data yet.
constexpr float kGnssDriveKmh = 10.0f;      // speed that may start a trip...
constexpr float kGnssLeaveM = 150.0f;       // ...but only this far from where it parked
constexpr float kGnssStayM = 50.0f;         // within this radius for kGnssStopMs: parked
constexpr uint32_t kGnssStopMs = 180000UL;  // standing this long: trip_end
constexpr uint32_t kGnssLostMs = 600000UL;  // no fix this long: trip_end too

struct Anchor {
  bool set = false;
  int32_t lat_e7 = 0, lon_e7 = 0;
};

// Everything the decision remembers between samples.
struct State {
  Anchor park;  // where the car parked, or the first fix since boot
  Anchor stop;  // centre of the circle a driving car may be standing in
  uint32_t slow_since_ms = 0;
  uint32_t nofix_since_ms = 0;
};

enum class Change { kNone, kTripStart, kTripEnd };

inline void setAnchor(Anchor& a, int32_t lat_e7, int32_t lon_e7) {
  a.set = true;
  a.lat_e7 = lat_e7;
  a.lon_e7 = lon_e7;
}

// Equirectangular distance; plenty for a few hundred metres.
inline float metresFrom(const Anchor& a, int32_t lat_e7, int32_t lon_e7) {
  const float lat = lat_e7 * 1e-7f * 0.0174533f;
  const float dy = (lat_e7 - a.lat_e7) * 1e-7f * 110540.0f;
  const float dx = (lon_e7 - a.lon_e7) * 1e-7f * 111320.0f * cosf(lat);
  return sqrtf(dx * dx + dy * dy);
}

// One sample. `driving` is the current mode (true for DRIVING), `now` a
// millisecond clock, `fix` whether lat/lon/kmh are valid. The caller applies
// the returned change to its mode.
inline Change update(State& s, bool driving, uint32_t now, bool fix, int32_t lat_e7,
                     int32_t lon_e7, float kmh) {
  if (driving) {
    // A lost fix (tunnel, garage) is not a stop; only a long one ends the trip.
    if (!fix) {
      if (s.nofix_since_ms == 0) s.nofix_since_ms = now;
      if (now - s.nofix_since_ms > kGnssLostMs) {
        s.nofix_since_ms = 0;
        s.park.set = false;
        return Change::kTripEnd;
      }
      return Change::kNone;
    }
    s.nofix_since_ms = 0;
    if (!s.stop.set || metresFrom(s.stop, lat_e7, lon_e7) > kGnssStayM) {
      setAnchor(s.stop, lat_e7, lon_e7);
      s.slow_since_ms = now;
    } else if (now - s.slow_since_ms > kGnssStopMs) {
      setAnchor(s.park, lat_e7, lon_e7);
      s.stop.set = false;
      return Change::kTripEnd;
    }
    return Change::kNone;
  }

  if (!fix) return Change::kNone;
  if (!s.park.set) {
    setAnchor(s.park, lat_e7, lon_e7);  // first fix since boot or since parking
    return Change::kNone;
  }
  if (kmh >= kGnssDriveKmh && metresFrom(s.park, lat_e7, lon_e7) > kGnssLeaveM) {
    s.stop.set = false;
    return Change::kTripStart;
  }
  return Change::kNone;
}

}  // namespace gnssmode
