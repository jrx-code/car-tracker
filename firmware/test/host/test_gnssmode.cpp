// Host-side check of the GNSS-only trip decision (util/gnssmode.h), run in CI:
//   g++ -std=c++17 -Wall -I src test/host/test_gnssmode.cpp -o /tmp/t && /tmp/t
// A board lying still opened a trip from speed jitter on 2026-10-08 and, while
// "driving", refuses OTA, so the noise cases matter as much as the real ones.
#include <cmath>
#include <cstdio>

#include "util/gnssmode.h"

namespace {

using gnssmode::Change;

int failed = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failed;
  }
}

// Example position (docs use 52.0, 21.0), offsets in metres east/north.
constexpr double kLat0 = 52.0, kLon0 = 21.0;
int32_t latE7(double north_m) { return static_cast<int32_t>(std::lround((kLat0 + north_m / 110540.0) * 1e7)); }
int32_t lonE7(double east_m) {
  return static_cast<int32_t>(
      std::lround((kLon0 + east_m / (111320.0 * std::cos(kLat0 * 3.14159265358979 / 180.0))) * 1e7));
}

// The mode as main.cpp keeps it, driven only by what update() returns.
struct Sim {
  gnssmode::State s;
  bool driving = false;
  int starts = 0, ends = 0;
  Change step(uint32_t now, bool fix, double north_m, double east_m, float kmh) {
    const Change c = gnssmode::update(s, driving, now, fix, latE7(north_m), lonE7(east_m), kmh);
    if (c == Change::kTripStart) { driving = true; ++starts; }
    if (c == Change::kTripEnd) { driving = false; ++ends; }
    return c;
  }
};

// Deterministic jitter in [-1, 1].
double jitter(int i, int salt) { return std::sin(i * 12.9898 + salt * 78.233); }

void testStandingNoiseDoesNotStartTrip() {
  // Bench, 2026-10-08: board lying still, speed 0..7 km/h (6.87 and 3.89 seen)
  // at HDOP 1.7-2.3, position scattered by about 20 m. One hour at the 5 s
  // parked sampling period.
  Sim sim;
  const float speeds[] = {0.0f, 6.87f, 3.89f, 1.2f, 5.5f, 0.4f, 7.0f, 2.3f};
  for (int i = 0; i < 720; i++) {
    sim.step(i * 5000u, true, 20.0 * jitter(i, 1), 20.0 * jitter(i, 2), speeds[i % 8]);
  }
  check(sim.starts == 0, "standing board with 0-7 km/h noise opened a trip");

  // Same scatter with an occasional speed spike above the threshold: still no
  // trip, because the position never leaves the parking spot.
  Sim spikes;
  for (int i = 0; i < 720; i++) {
    const float kmh = (i % 37 == 0) ? 14.0f : speeds[i % 8];
    spikes.step(i * 5000u, true, 20.0 * jitter(i, 3), 20.0 * jitter(i, 4), kmh);
  }
  check(spikes.starts == 0, "speed spike without displacement opened a trip");
}

void testLeavingStartsTrip() {
  Sim sim;
  sim.step(0, true, 0, 0, 0);  // first fix: parking anchor
  // Rolls away at 12 km/h (3.3 m/s), one sample per 5 s.
  uint32_t now = 0;
  double north = 0;
  bool started_early = false;
  while (!sim.driving && now < 600000u) {
    now += 5000;
    north += 3.33 * 5;
    sim.step(now, true, north, 0, 12.0f);
    if (sim.driving && north <= 150.0) started_early = true;
  }
  check(sim.starts == 1, "leaving more than 150 m at >= 10 km/h did not open a trip");
  check(!started_early, "trip opened before 150 m from the parking spot");

  // Far away but slow (pushed, or GNSS drift after a long gap): no trip.
  Sim slow;
  slow.step(0, true, 0, 0, 0);
  slow.step(5000, true, 400, 0, 9.9f);
  check(slow.starts == 0, "trip opened below 10 km/h");
  slow.step(10000, true, 400, 0, 10.0f);
  check(slow.starts == 1, "10 km/h far from the parking spot did not open a trip");
}

// A Sim that is driving, positioned at (0, 0) at time t0.
Sim drivingAt(uint32_t& t0) {
  Sim sim;
  sim.step(0, true, 0, 0, 0);
  sim.step(5000, true, -300, 0, 40.0f);
  t0 = 5000;
  return sim;
}

void testStandingInCircleEndsTrip() {
  uint32_t t = 0;
  Sim sim = drivingAt(t);
  check(sim.driving, "setup: not driving");
  // Stops: samples every second inside a 50 m circle with ~20 m scatter.
  const uint32_t stop_at = t + 1000;
  bool ended_early = false;
  for (uint32_t now = stop_at; now <= stop_at + 200000u; now += 1000) {
    sim.step(now, true, -300 + 20.0 * jitter(now / 1000, 5), 20.0 * jitter(now / 1000, 6), 3.0f);
    if (!sim.driving && now - stop_at < 180000u) ended_early = true;
  }
  check(sim.ends == 1, "3 min inside a 50 m circle did not end the trip");
  check(!ended_early, "trip ended before 3 min of standing");

  // Crawling in traffic, leaving the circle every minute or so: stays a trip.
  Sim crawl = drivingAt(t);
  double north = -300;
  for (uint32_t now = t + 1000; now < t + 1800000u; now += 1000) {
    north += 0.9;  // ~3 km/h, 54 m a minute
    crawl.step(now, true, north, 0, 3.0f);
  }
  check(crawl.ends == 0, "slow traffic that keeps moving ended the trip");
}

void testLostFix() {
  uint32_t t = 0;
  // 10 min without a fix while driving ends the trip.
  Sim sim = drivingAt(t);
  bool ended_early = false;
  for (uint32_t now = t + 1000; now <= t + 1000 + 610000u; now += 1000) {
    sim.step(now, false, 0, 0, 0);
    if (!sim.driving && now - (t + 1000) < 600000u) ended_early = true;
  }
  check(sim.ends == 1, "10 min without a fix did not end the trip");
  check(!ended_early, "trip ended before 10 min without a fix");
  check(!sim.s.park.set, "parking anchor kept after a lost-fix trip end");

  // 2 min without a fix (tunnel) and then moving again: the trip goes on.
  Sim tunnel = drivingAt(t);
  uint32_t now = t + 1000;
  double north = -300;
  for (; now <= t + 1000 + 120000u; now += 1000) {
    north += 15;  // the car keeps going through the tunnel
    tunnel.step(now, false, 0, 0, 0);
  }
  for (int i = 0; i < 120; i++, now += 1000) {
    north += 15;  // 54 km/h
    tunnel.step(now, true, north, 0, 54.0f);
  }
  check(tunnel.ends == 0, "2 min without a fix ended the trip");
  check(tunnel.driving, "not driving after a tunnel");

  // Two short gaps that together exceed 10 min are not one long gap.
  Sim gaps = drivingAt(t);
  now = t + 1000;
  north = -300;
  for (int round = 0; round < 3; round++) {
    // Driving on through each gap, so the next fix is far from the last one.
    for (uint32_t end = now + 400000u; now < end; now += 1000) {
      north += 15;
      gaps.step(now, false, 0, 0, 0);
    }
    for (int i = 0; i < 10; i++, now += 1000) {
      north += 15;
      gaps.step(now, true, north, 0, 54.0f);
    }
  }
  check(gaps.ends == 0, "separate gaps under 10 min added up to a trip end");
}

void testNoFixWhileParkedDoesNothing() {
  Sim sim;
  for (uint32_t now = 0; now < 3600000u; now += 5000) sim.step(now, false, 0, 0, 0);
  check(sim.starts == 0 && sim.ends == 0, "no fix while parked changed the mode");
  check(!sim.s.park.set, "parking anchor set without a fix");
}

}  // namespace

int main() {
  testStandingNoiseDoesNotStartTrip();
  testLeavingStartsTrip();
  testStandingInCircleEndsTrip();
  testLostFix();
  testNoFixWhileParkedDoesNothing();
  std::printf("%s\n", failed ? "FAILED" : "ok");
  return failed ? 1 : 0;
}
