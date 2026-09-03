// Copy to config.h and fill in. config.h is gitignored, never commit real values.
// Runtime-changeable settings do NOT belong here, they live in NVS and arrive on
// the retained cfg topic (see docs/05-protokol-mqtt.md section 5.6).
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

// --- WiFi (bench build and OTA in the garage) ------------------------------
#define WIFI_SSID "REPLACE_ME"
#define WIFI_PASS "REPLACE_ME"

// --- LTE ------------------------------------------------------------------
#define GSM_APN "internet"      // depends on the SIM operator
#define GSM_APN_USER ""
#define GSM_APN_PASS ""
#define GSM_SIM_PIN ""          // empty = PIN disabled on the card, preferred, see docs/09

// --- TLS ------------------------------------------------------------------
// Root CA of the broker certificate. Pinning the CA and not the leaf, so that a
// cert-engine renewal does not strand the tracker on the road (docs/09 section 9.3).
static const char MQTT_ROOT_CA[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
REPLACE_ME
-----END CERTIFICATE-----
)EOF";
