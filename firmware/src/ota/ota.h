// Firmware update over the existing MQTT link, LTE or WiFi (docs/07, 7.7).
//
// The image travels as chunks the device asks for one by one, on the same
// authenticated TLS session it already has: no extra port, no second host to
// reach from a carrier network. It is written to the inactive OTA slot, so an
// interrupted transfer leaves the running firmware untouched, and it is only
// booted when its SHA-256 matches the value signed with the release key.
// The bootloader has no rollback (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE is off
// in this Arduino core), so the trial boot is tracked here: a new image that
// does not reach the broker, or crashes repeatedly, is switched back out.
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

namespace ota {

// Topics are owned by main.cpp; the strings must outlive the module.
void begin(const char* req_topic, const char* data_topic, const char* ack_topic);

// Checks an "ota" command (settings, vehicle state, size, signature) and queues
// it for service(). Returns false with the reason in `why`.
bool queue(JsonVariantConst cmd, bool parked, char* why, size_t why_len);

bool pending();

// Runs a queued update to the end: minutes, blocking, called from loop() and
// never from inside the MQTT callback. Reboots on success.
void service();

// A chunk from <prefix>/<id>/ota/data: 4-byte little-endian offset + bytes.
void onData(const uint8_t* payload, unsigned len);

// Early in setup(): counts boots of an unconfirmed image, rolls back after
// kMaxTrialBoots.
void bootCheck();

// After the broker accepted the connection. True when this boot confirmed a
// fresh image (worth an event).
bool confirm();

// From loop(): rolls back an image that has not been confirmed in time.
void loopCheck();

}  // namespace ota
