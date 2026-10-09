// Sequence number reservation, plain C++ on purpose (no Arduino headers), so
// test/host/ can compile it on the CI runner.
//
// seq must never repeat: the hub drops a (vehicle, seq) it already has, so a
// reused number silently loses a position. The old code saved seq only when a
// position happened to land on a multiple of 16, telemetry and events advanced
// it without saving, and every boot restarted from the same stale value (113
// on each restart in the 2026-10-08 field test). Now a block of 16 is reserved
// in NVS before any number in it is used, and a boot starts past the
// reservation: numbers may be skipped, never reused. One NVS write per 16.
#pragma once

#include <cstdint>

namespace seqreserve {

// Numbers reserved ahead of the one being handed out.
constexpr uint32_t kBlock = 16;
// The old "seq" key was a stale lower bound, so a board coming from an older
// build skips well past it once.
constexpr uint32_t kLegacySkip = 1000;

// Persists a reservation (NVS key "seq2" on the device). A plain function
// pointer, so the host test can count and record the writes.
using SaveFn = void (*)(uint32_t reserved);

// Where numbering resumes at boot. `rtc` is the counter kept in RTC memory,
// which survives deep sleep but not a power cut (then it is 0).
// `has_reservation`/`reservation` is the "seq2" key, `legacy` the old "seq".
inline uint32_t bootValue(uint32_t rtc, bool has_reservation, uint32_t reservation,
                          uint32_t legacy) {
  const uint32_t stored = has_reservation ? reservation : legacy + kLegacySkip;
  return rtc > stored ? rtc : stored;
}

// Next number. `seq` is the last number handed out, `reserved` the highest
// number already covered by a saved reservation; at boot both start at
// bootValue(), since everything up to there may have been used. The
// reservation is saved before the number that needs it is returned.
inline uint32_t next(uint32_t& seq, uint32_t& reserved, SaveFn save) {
  if (++seq > reserved) {
    reserved = seq + kBlock;
    save(reserved);
  }
  return seq;
}

}  // namespace seqreserve
