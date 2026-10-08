#include "ota/ota.h"

#include <Preferences.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <mbedtls/base64.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>

#include "modem/transport.h"
#include "ota/ota_pubkey.h"
#include "settings/settings.h"
#include "telemetry/packet.h"

namespace ota {
namespace {

constexpr uint32_t kChunkTimeoutMs = 15000;
constexpr int kChunkRetries = 4;
constexpr uint8_t kMaxTrialBoots = 3;
constexpr uint32_t kConfirmWindowMs = 10UL * 60UL * 1000UL;
constexpr uint16_t kChunkMin = 256;
constexpr uint16_t kChunkMax = 1536;  // MQTT_MAX_PACKET_SIZE is 2048

struct Request {
  uint32_t size = 0;
  uint8_t sha256[32] = {};
  uint8_t sig[80] = {};  // DER ECDSA P-256 is at most 72 bytes
  size_t sig_len = 0;
  uint16_t chunk = 1024;
  char id[16] = "";
};

Request req;
bool queued = false;
bool trial = false;

const char* t_req = "";
const char* t_data = "";
const char* t_ack = "";

// One chunk in flight at a time: the device asks for offset N and accepts
// only the reply for N, so a late or duplicated chunk cannot land elsewhere.
uint8_t chunk_buf[kChunkMax];
size_t chunk_len = 0;
uint32_t chunk_off = UINT32_MAX;
bool chunk_ready = false;

void reply(const char* id, bool ok, uint32_t ms, const char* msg) {
  char b[256];
  if (packet::buildAck(id, ok, ms, msg, b, sizeof(b))) transport::publish(t_ack, b, false);
}

bool hexToBytes(const char* hex, uint8_t* out, size_t n) {
  if (strlen(hex) != n * 2) return false;
  for (size_t i = 0; i < n; i++) {
    char pair[3] = {hex[2 * i], hex[2 * i + 1], 0};
    char* end = nullptr;
    const long v = strtol(pair, &end, 16);
    if (*end != '\0') return false;
    out[i] = static_cast<uint8_t>(v);
  }
  return true;
}

// The PEM text is the single source of the key (ota_release.sh compares it
// with the signing key), but this core builds mbedtls without PEM parsing
// (CONFIG_MBEDTLS_PEM_PARSE_C off), so every signature was "not valid"
// (2026-10-08). The base64 body is decoded here and parsed as DER.
size_t publicKeyDer(uint8_t* der, size_t der_len) {
  char b64[160] = "";
  size_t n = 0;
  for (const char* line = kOtaPublicKeyPem; *line;) {
    const char* end = strchr(line, '\n');
    const size_t len = end ? static_cast<size_t>(end - line) : strlen(line);
    if (len && line[0] != '-' && n + len < sizeof(b64)) {
      memcpy(b64 + n, line, len);
      n += len;
    }
    line += len + (end ? 1 : 0);
  }
  size_t out = 0;
  if (mbedtls_base64_decode(der, der_len, &out, reinterpret_cast<unsigned char*>(b64), n) != 0) {
    return 0;
  }
  return out;
}

bool signatureValid(const uint8_t hash[32], const uint8_t* sig, size_t sig_len) {
  uint8_t der[128];
  const size_t der_len = publicKeyDer(der, sizeof(der));
  mbedtls_pk_context pk;
  mbedtls_pk_init(&pk);
  int rc = der_len ? mbedtls_pk_parse_public_key(&pk, der, der_len) : -1;
  const bool key_ok = rc == 0;
  if (key_ok) rc = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, 32, sig, sig_len);
  mbedtls_pk_free(&pk);
  if (rc != 0) {
    Serial.printf("ota: %s failed, mbedtls -0x%04x\n", key_ok ? "verify" : "key parse",
                  static_cast<unsigned>(-rc));
  }
  return rc == 0;
}

void clearTrial() {
  Preferences p;
  p.begin("ota", false);
  p.putBool("trial", false);
  p.putUChar("boots", 0);
  p.end();
  trial = false;
}

void rollBack(const char* why) {
  Serial.printf("ota: rolling back (%s)\n", why);
  clearTrial();
  // Update.rollBack() points the boot record at the other slot, which still
  // holds the image that ran before this update.
  if (Update.rollBack()) {
    delay(200);
    ESP.restart();
  }
  Serial.println("ota: nothing to roll back to, staying on this image");
}

}  // namespace

void begin(const char* req_topic, const char* data_topic, const char* ack_topic) {
  t_req = req_topic;
  t_data = data_topic;
  t_ack = ack_topic;
}

bool queue(JsonVariantConst cmd, bool parked, char* why, size_t why_len) {
  auto fail = [&](const char* msg) {
    snprintf(why, why_len, "ota: %s", msg);
    return false;
  };
  if (!settings::get().ota_enabled) return fail("disabled in settings");
  // A moving car is where a half-done update hurts most; parked only.
  if (!parked) return fail("refused while driving");
  if (queued) return fail("an update is already queued");

  const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
  const uint32_t size = cmd["size"] | 0u;
  if (next == nullptr) return fail("no OTA partition");
  if (size == 0 || size > next->size) return fail("image size does not fit the slot");

  Request r;
  if (!hexToBytes(cmd["sha256"] | "", r.sha256, sizeof(r.sha256))) return fail("bad sha256");
  const char* sig_b64 = cmd["sig"] | "";
  if (mbedtls_base64_decode(r.sig, sizeof(r.sig), &r.sig_len,
                            reinterpret_cast<const unsigned char*>(sig_b64),
                            strlen(sig_b64)) != 0 ||
      r.sig_len == 0) {
    return fail("bad signature encoding");
  }
  // Checked before a single byte is downloaded: an image not signed with the
  // release key is never fetched, let alone written.
  if (!signatureValid(r.sha256, r.sig, r.sig_len)) return fail("signature not valid");

  const uint16_t chunk = cmd["chunk"] | 1024u;
  r.chunk = constrain(chunk, kChunkMin, kChunkMax);
  r.size = size;
  strncpy(r.id, cmd["id"] | "", sizeof(r.id) - 1);
  req = r;
  queued = true;
  return true;
}

bool pending() { return queued; }

void onData(const uint8_t* payload, unsigned len) {
  if (len < 4 || chunk_ready) return;
  const uint32_t off = static_cast<uint32_t>(payload[0]) |
                       static_cast<uint32_t>(payload[1]) << 8 |
                       static_cast<uint32_t>(payload[2]) << 16 |
                       static_cast<uint32_t>(payload[3]) << 24;
  const size_t n = len - 4;
  if (off != chunk_off || n > sizeof(chunk_buf)) return;
  memcpy(chunk_buf, payload + 4, n);
  chunk_len = n;
  chunk_ready = true;
}

void service() {
  if (!queued) return;
  queued = false;
  const uint32_t t0 = millis();
  Serial.printf("ota: %lu bytes in %u byte chunks\n", static_cast<unsigned long>(req.size),
                req.chunk);
  reply(req.id, true, 0, "ota: downloading");
  transport::subscribe(t_data);

  if (!Update.begin(req.size)) {
    reply(req.id, false, millis() - t0, "ota: cannot open the update slot");
    return;
  }
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);

  uint32_t off = 0;
  const char* err = nullptr;
  while (off < req.size && err == nullptr) {
    const uint32_t want = min<uint32_t>(req.chunk, req.size - off);
    bool got = false;
    for (int attempt = 0; attempt < kChunkRetries && !got; attempt++) {
      chunk_ready = false;
      chunk_off = off;
      char ask[48];
      snprintf(ask, sizeof(ask), "{\"off\":%lu,\"len\":%lu}", static_cast<unsigned long>(off),
               static_cast<unsigned long>(want));
      if (!transport::publish(t_req, ask, false)) {
        transport::loop();
        delay(1000);
        continue;
      }
      const uint32_t w0 = millis();
      while (millis() - w0 < kChunkTimeoutMs && !chunk_ready) {
        transport::loop();
        delay(2);
      }
      got = chunk_ready && chunk_len == want;
    }
    if (!got) {
      err = "ota: chunk did not arrive";
    } else if (Update.write(chunk_buf, chunk_len) != chunk_len) {
      err = "ota: flash write failed";
    } else {
      mbedtls_sha256_update(&sha, chunk_buf, chunk_len);
      off += chunk_len;
    }
  }
  chunk_off = UINT32_MAX;

  uint8_t digest[32];
  mbedtls_sha256_finish(&sha, digest);
  mbedtls_sha256_free(&sha);
  if (err == nullptr && memcmp(digest, req.sha256, sizeof(digest)) != 0) {
    err = "ota: image hash does not match the signed one";
  }
  if (err != nullptr) {
    Update.abort();
    Serial.println(err);
    reply(req.id, false, millis() - t0, err);
    return;
  }
  // end() checks the image header and makes the new slot the boot slot.
  if (!Update.end()) {
    char msg[96];
    snprintf(msg, sizeof(msg), "ota: %s", Update.errorString());
    reply(req.id, false, millis() - t0, msg);
    return;
  }

  Preferences p;
  p.begin("ota", false);
  p.putBool("trial", true);
  p.putUChar("boots", 0);
  p.end();

  char msg[96];
  snprintf(msg, sizeof(msg), "ota: %lu bytes in %lu s, rebooting into the new image",
           static_cast<unsigned long>(req.size), static_cast<unsigned long>((millis() - t0) / 1000));
  Serial.println(msg);
  reply(req.id, true, millis() - t0, msg);
  for (int i = 0; i < 20; i++) {
    transport::loop();
    delay(50);
  }
  ESP.restart();
}

void bootCheck() {
  Preferences p;
  p.begin("ota", false);
  trial = p.getBool("trial", false);
  uint8_t boots = 0;
  if (trial) {
    boots = p.getUChar("boots", 0) + 1;
    p.putUChar("boots", boots);
  }
  p.end();
  if (!trial) return;
  Serial.printf("ota: trial boot %u of %u of a new image\n", boots, kMaxTrialBoots);
  if (boots > kMaxTrialBoots) rollBack("new image keeps restarting");
}

bool confirm() {
  if (!trial) return false;
  clearTrial();
  Serial.println("ota: new image confirmed");
  return true;
}

void loopCheck() {
  if (trial && millis() > kConfirmWindowMs) rollBack("new image did not reach the broker");
}

}  // namespace ota
