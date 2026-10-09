// Host-side check of the seq reservation (util/seqreserve.h), run in CI:
//   g++ -std=c++17 -Wall -I src test/host/test_seqreserve.cpp -o /tmp/t && /tmp/t
// A repeated seq is a position the hub silently drops (2026-10-08), so the
// cases below restart the "board" in every way it can restart.
#include <cstdio>
#include <set>
#include <vector>

#include "util/seqreserve.h"

namespace {

int failed = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failed;
  }
}

// Fake NVS: the last saved reservation and how often it was written.
bool nvs_has = false;
uint32_t nvs_seq2 = 0;
int nvs_writes = 0;

void fakeSave(uint32_t reserved) {
  nvs_has = true;
  nvs_seq2 = reserved;
  ++nvs_writes;
}

// One boot of the board: the RAM state of main.cpp (rtc_seq, seq_reserved).
struct Board {
  uint32_t seq = 0;
  uint32_t reserved = 0;
  void boot(uint32_t rtc, uint32_t legacy = 0) {
    seq = seqreserve::bootValue(rtc, nvs_has, nvs_seq2, legacy);
    reserved = seq;
  }
  // Hands out n numbers, checking each is covered by a saved reservation
  // at the moment it is returned.
  std::vector<uint32_t> take(int n) {
    std::vector<uint32_t> out;
    for (int i = 0; i < n; i++) {
      const uint32_t v = seqreserve::next(seq, reserved, fakeSave);
      check(nvs_has && v <= nvs_seq2, "number used before its reservation was saved");
      out.push_back(v);
    }
    return out;
  }
};

void resetNvs() {
  nvs_has = false;
  nvs_seq2 = 0;
  nvs_writes = 0;
}

void testIncreasing() {
  resetNvs();
  Board b;
  b.boot(0);
  const auto v = b.take(100);
  bool up = true;
  for (size_t i = 1; i < v.size(); i++) up = up && v[i] == v[i - 1] + 1;
  check(up, "numbers increase by one within a boot");
  check(v.front() > 0, "first number is above zero");
}

void testNoRepeatAcrossRestarts() {
  resetNvs();
  std::set<uint32_t> seen;
  Board b;
  b.boot(0);
  uint32_t last = 0;
  // Power cuts at every offset inside a block, and a few deep-sleep wakes in
  // between, where the RTC counter survives.
  for (int round = 0; round < 40; round++) {
    const auto v = b.take(round % 23 + 1);
    for (uint32_t n : v) {
      check(seen.insert(n).second, "a number repeated after a restart");
      check(n > last, "a number went backwards after a restart");
      last = n;
    }
    if (round % 5 == 4) {
      b.boot(b.seq);  // deep sleep: RTC memory kept, RAM reservation lost
    } else {
      Board fresh;    // power cut: RTC memory gone, only NVS left
      fresh.boot(0);
      b = fresh;
    }
  }
}

void testOneWritePerBlock() {
  resetNvs();
  Board b;
  b.boot(0);
  const int n = 16 * 50;
  b.take(n);
  // Each write covers the number that triggered it plus kBlock ahead.
  check(nvs_writes <= (n + 15) / 16, "more than one NVS write per 16 numbers");
  check(nvs_writes >= n / 17, "fewer writes than a block can cover");

  resetNvs();
  Board c;
  c.boot(0);
  int prev_writes = 0;
  int since = 0;
  bool spaced = true;
  for (int i = 0; i < 200; i++) {
    c.take(1);
    ++since;
    if (nvs_writes != prev_writes) {
      if (prev_writes > 0 && since < 16) spaced = false;
      prev_writes = nvs_writes;
      since = 0;
    }
  }
  check(spaced, "two NVS writes less than 16 numbers apart");
}

void testLegacyMigration() {
  resetNvs();
  Board b;
  b.boot(0, 113);  // older build: only "seq" = 113, no "seq2"
  const auto v = b.take(1);
  check(v[0] == 113 + seqreserve::kLegacySkip + 1, "legacy seq not skipped by 1000");
  check(nvs_has && nvs_seq2 >= v[0], "migration did not write seq2");

  // Once seq2 exists the legacy key is ignored, however large it is.
  Board again;
  const uint32_t saved = nvs_seq2;
  again.boot(0, 999999);
  check(again.take(1)[0] == saved + 1, "legacy key read although seq2 exists");

  // The RTC counter wins when it is ahead of what NVS says.
  resetNvs();
  Board rtc;
  rtc.boot(5000, 113);
  check(rtc.take(1)[0] == 5001, "RTC counter ahead of NVS not honoured");
}

}  // namespace

int main() {
  testIncreasing();
  testNoRepeatAcrossRestarts();
  testOneWritePerBlock();
  testLegacyMigration();
  std::printf("%s\n", failed ? "FAILED" : "ok");
  return failed ? 1 : 0;
}
