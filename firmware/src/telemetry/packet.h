// Payload building. The wire format is defined in docs/05-protokol-mqtt.md and
// the HA integration parses exactly this; change both together.
#pragma once
#include <Arduino.h>

#include "modem/transport.h"
#include "state.h"

namespace packet {

// Full JSON position. compact=true shortens the keys and drops unchanged
// optional fields, which is what keeps a sample under 120 bytes (docs/05 5.8).
size_t buildPos(const PosRecord& rec, char* out, size_t out_len, bool compact);

size_t buildTel(const Telemetry& tel, VehicleMode mode, uint32_t seq,
                uint32_t ts, char* out, size_t out_len);

size_t buildEvent(const char* ev, uint32_t seq, uint32_t ts, const PosRecord* pos,
                  char* out, size_t out_len);

size_t buildInfo(const transport::LinkInfo& link, const char* fw_version,
                 const char* modem_name, char* out, size_t out_len);

size_t buildBatch(const PosRecord* recs, uint16_t n, char* out, size_t out_len,
                  bool compact);

size_t buildAck(const char* id, bool ok, uint32_t ms, const char* msg, char* out,
                size_t out_len);

// Apply a cfg payload onto the config struct. Unknown keys are ignored, missing
// keys keep their current value, so a partial cfg is a valid cfg.
bool applyConfig(const uint8_t* payload, unsigned len, Config& cfg);

}  // namespace packet
