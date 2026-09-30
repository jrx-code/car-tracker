// Copy to config.h and fill in. config.h is gitignored, never commit real values.
// Runtime-changeable settings do NOT belong here, they live in NVS and arrive on
// the retained cfg topic (see docs/05-protokol-mqtt.md section 5.6).
//
// PoC (wifi_dev): copy this file, fill WIFI_*, MQTT_* and MQTT_ROOT_CA from your
// password manager, then `pio run -e wifi_dev -t upload`. See docs/00-poc.md and
// docs/examples/poc-wifi.md. LTE / APN fields below are unused on wifi_dev.
#pragma once

// --- Identity -------------------------------------------------------------
// Fallback only. The real vehicle id is stored in NVS so one binary serves both
// cars (assumption Z6). Set it once per device with the set_id command.
#define DEFAULT_VEHICLE_ID "nd1"

// Registration and VIN of the car this board is fitted to. Both are editable in
// the portal; these are only what a blank device starts with. Leave empty on a
// bench board that is not in a car yet.
#define DEFAULT_PLATE ""
#define DEFAULT_VIN ""

// --- MQTT broker ----------------------------------------------------------
#define MQTT_HOST "mqtt.example.lan"
#define MQTT_PORT 8883
#define MQTT_USER "cartracker-nd1"   // one user per vehicle, see docs/09 section 9.2
#define MQTT_PASS "REPLACE_ME"       // from your password manager, never in the repo
#define MQTT_KEEPALIVE 120

// --- WiFi (bench build / PoC wifi_dev, and OTA in the garage) --------------
// Hotspot from a phone in the car, or garage WiFi. Same firmware transport path
// as LTE; only the link layer differs (docs/00).
#define WIFI_SSID "REPLACE_ME"
#define WIFI_PASS "REPLACE_ME"

// --- LTE ------------------------------------------------------------------
// Unused on wifi_dev. Filled when bringing up a modem board (docs/03).
#define GSM_APN "internet"      // depends on the SIM operator
#define GSM_APN_USER ""
#define GSM_APN_PASS ""
#define GSM_SIM_PIN ""          // empty = PIN disabled on the card, preferred, see docs/09

// --- TLS ------------------------------------------------------------------
// Root CA of the broker certificate. Pin the CA (not the leaf) so a cert-engine
// renewal does not strand the tracker on the road (docs/09 section 9.3).
//
// Tip: take the CA from the chain the broker actually sends, not a generic root
// of the same brand. On BearSSL (ESP32) the wrong cross-signer fails TLS even
// when openssl s_client looks fine. How to extract it: docs/13 section 13.6
// (broker certificate pitfall; CLAUDE.md still says 13.5 from an older numbering).
static const char MQTT_ROOT_CA[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
REPLACE_ME
-----END CERTIFICATE-----
)EOF";
