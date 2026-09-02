// Runtime settings, owned by NVS and editable from the web portal.
//
// Before this existed, everything lived in config.h and every change meant a
// recompile and a cable. With two cars and a device that will sit under a
// dashboard, that is the wrong place for it: credentials, the broker address
// and even the GNSS pins now live in NVS and are edited in the browser.
//
// config.h keeps its role as the FACTORY DEFAULT: the values it holds are used
// on a blank device and after a factory reset, and nothing else.
//
// Secrets never travel back to the browser. The portal reports whether a
// password is set, never what it is.
#pragma once
#include <Arduino.h>

namespace settings {

constexpr size_t kStrLen = 64;
constexpr size_t kPassLen = 64;
constexpr size_t kIdLen = 16;

struct Settings {
  // --- identity ---
  char vehicle_id[kIdLen];    // forms the MQTT topic, must match the HA entry
  char vehicle_name[kStrLen]; // shown in the portal only
  char hostname[kStrLen];     // mDNS and DHCP name

  // --- WiFi (garage, OTA, and the whole PoC) ---
  bool wifi_enabled;
  char wifi_ssid[kStrLen];
  char wifi_pass[kPassLen];
  uint16_t wifi_timeout_s;    // how long to try before falling back

  // --- emergency access point ---
  // Without this, a device that loses its WiFi and has no working modem is a
  // brick under a dashboard: no console, no network, nothing to connect to.
  bool ap_enabled;
  char ap_ssid[kStrLen];      // empty = cartracker-<vehicle_id>
  char ap_pass[kPassLen];     // empty = open network, refused below 8 chars
  uint16_t ap_after_s;        // seconds offline before the AP comes up
  uint16_t ap_timeout_s;      // 0 = keep it up, otherwise shut down after this

  // --- MQTT ---
  char mqtt_host[kStrLen];
  uint16_t mqtt_port;
  char mqtt_user[kStrLen];
  char mqtt_pass[kPassLen];
  bool mqtt_tls;
  bool mqtt_verify_ca;        // off = encrypted but unauthenticated, for a bench
  char topic_prefix[kStrLen];
  uint16_t mqtt_keepalive;

  // --- LTE, used once a modem exists ---
  bool modem_enabled;
  char apn[kStrLen];
  char apn_user[kStrLen];
  char apn_pass[kPassLen];
  char sim_pin[kIdLen];
  bool allow_roaming;

  // --- hardware pins ---
  // Editable on purpose. A whole afternoon went into a receiver that was silent
  // for a firmware reason; being able to move a pin from the browser is worth
  // more than the tidiness of a compile-time constant.
  int8_t pin_gnss_rx;
  int8_t pin_gnss_tx;
  int8_t pin_gnss_en;
  uint32_t gnss_baud;
  int8_t pin_modem_rx;
  int8_t pin_modem_tx;
  int8_t pin_modem_pwrkey;
  int8_t pin_modem_en;
  int8_t pin_vbat_adc;
  int8_t pin_acc_int;
  int8_t pin_i2c_sda;
  int8_t pin_i2c_scl;
  int8_t pin_led;

  // --- portal and OTA ---
  char admin_pass[kPassLen];  // required to WRITE settings, not to view them
  bool portal_enabled;
  bool ota_enabled;

  uint8_t version;            // struct version, for migrations
};

// Load from NVS, falling back to the config.h defaults for anything missing.
void begin();

Settings& get();

// Persist the current struct. Returns false on an NVS error.
bool save();

// Restore the config.h defaults and persist them.
void factoryReset();

// Apply a JSON body from the portal. Only the keys present are touched, so a
// partial form submission is valid. Fills `error` and returns false when a
// value is refused; nothing is written in that case.
bool applyJson(const String& json, String& error);

// Current settings as JSON for the portal. Passwords are replaced by a boolean
// "<key>_set" so the browser learns whether one exists, never what it is.
String toJson();

// The AP name actually used, resolving the empty-means-default rule.
String apSsid();

bool adminPasswordMatches(const String& candidate);

// CA certificate for the broker. Stored in LittleFS, not NVS: a PEM chain is
// larger than an NVS entry comfortably holds.
String caCert();
bool setCaCert(const String& pem, String& error);
bool hasCaCert();

}  // namespace settings
