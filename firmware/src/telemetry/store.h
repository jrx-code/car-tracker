// Offline queue. Ring file in LittleFS with fixed size records, so a power cut
// in the middle of a write can lose at most one record and never corrupt the
// file structure (docs/02 section 2.6).
//
// A record is only dropped after the broker acknowledges it (QoS 1 PUBACK), so
// the failure mode is duplicates, not gaps. Duplicates are filtered by seq in
// the HA integration.
#pragma once
#include <Arduino.h>

#include "state.h"

namespace store {

constexpr uint16_t kCapacity = 20000;  // ~7 days of driving at 30 s intervals
constexpr uint8_t kBatchMax = 50;

bool begin();

// Append one position. Returns false only on a filesystem error; when the ring
// is full the oldest record is overwritten, because a fresh position is worth
// more than the oldest one.
bool push(const PosRecord& rec);

// Read up to max records without removing them.
uint16_t peek(PosRecord* out, uint16_t max);

// Drop the n oldest records, called after PUBACK.
void drop(uint16_t n);

uint16_t count();
void clear();

}  // namespace store
