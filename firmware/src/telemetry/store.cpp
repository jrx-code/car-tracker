#include "store.h"

#include <LittleFS.h>
#include <Preferences.h>

namespace store {
namespace {

constexpr const char* kPath = "/queue.bin";
constexpr uint8_t kStoreVersion = 1;

Preferences prefs;
uint32_t head = 0;  // index of the oldest record
uint32_t tail = 0;  // index one past the newest
bool ready = false;

void saveIndex() {
  prefs.begin("store", false);
  prefs.putUInt("head", head);
  prefs.putUInt("tail", tail);
  prefs.putUChar("ver", kStoreVersion);
  prefs.end();
}

size_t offsetOf(uint32_t index) {
  return static_cast<size_t>(index % kCapacity) * sizeof(PosRecord);
}

}  // namespace

bool begin() {
  if (!LittleFS.begin(true)) return false;

  prefs.begin("store", true);
  const uint8_t ver = prefs.getUChar("ver", 0);
  head = prefs.getUInt("head", 0);
  tail = prefs.getUInt("tail", 0);
  prefs.end();

  // A layout change invalidates every stored record; a wrong-sized read would
  // send garbage positions to HA, which is worse than losing the backlog.
  if (ver != kStoreVersion) {
    clear();
  }

  if (!LittleFS.exists(kPath)) {
    File f = LittleFS.open(kPath, "w");
    if (!f) return false;
    f.close();
  }
  ready = true;
  return true;
}

bool push(const PosRecord& rec) {
  if (!ready) return false;
  File f = LittleFS.open(kPath, "r+");
  if (!f) {
    f = LittleFS.open(kPath, "w+");
    if (!f) return false;
  }
  if (!f.seek(offsetOf(tail))) {
    f.close();
    return false;
  }
  const size_t n = f.write(reinterpret_cast<const uint8_t*>(&rec), sizeof(rec));
  f.close();
  if (n != sizeof(rec)) return false;

  tail++;
  if (tail - head > kCapacity) {
    head = tail - kCapacity;  // ring is full, drop the oldest
  }
  saveIndex();
  return true;
}

uint16_t peek(PosRecord* out, uint16_t max) {
  if (!ready || count() == 0) return 0;
  File f = LittleFS.open(kPath, "r");
  if (!f) return 0;

  const uint16_t n = min(static_cast<uint32_t>(max), tail - head);
  uint16_t got = 0;
  for (uint16_t i = 0; i < n; i++) {
    if (!f.seek(offsetOf(head + i))) break;
    if (f.read(reinterpret_cast<uint8_t*>(&out[i]), sizeof(PosRecord)) !=
        sizeof(PosRecord)) {
      break;
    }
    got++;
  }
  f.close();
  return got;
}

void drop(uint16_t n) {
  if (!ready) return;
  head += min(static_cast<uint32_t>(n), tail - head);
  if (head == tail) {
    head = tail = 0;  // keep the indices small and the file writes at offset 0
  }
  saveIndex();
}

uint16_t count() {
  const uint32_t c = tail - head;
  return static_cast<uint16_t>(min(c, static_cast<uint32_t>(kCapacity)));
}

void clear() {
  head = tail = 0;
  saveIndex();
  if (LittleFS.exists(kPath)) LittleFS.remove(kPath);
  File f = LittleFS.open(kPath, "w");
  if (f) f.close();
}

}  // namespace store
