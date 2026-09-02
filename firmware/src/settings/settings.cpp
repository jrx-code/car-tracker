#include "settings.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>

#include "config.h"
#include "pins.h"

namespace settings {
namespace {

constexpr uint8_t kVersion = 1;
constexpr const char* kNamespace = "cfg";
constexpr const char* kCaPath = "/ca.pem";

Preferences prefs;
Settings s;

void copyStr(char* dst, size_t len, const char* src) {
  strncpy(dst, src ? src : "", len - 1);
  dst[len - 1] = '\0';
}

void loadDefaults() {
  memset(&s, 0, sizeof(s));
  s.version = kVersion;

  copyStr(s.vehicle_id, kIdLen, DEFAULT_VEHICLE_ID);
  copyStr(s.vehicle_name, kStrLen, DEFAULT_VEHICLE_ID);
  copyStr(s.hostname, kStrLen, "cartracker");

  s.wifi_enabled = true;
  copyStr(s.wifi_ssid, kStrLen, WIFI_SSID);
  copyStr(s.wifi_pass, kPassLen, WIFI_PASS);
  s.wifi_timeout_s = 25;

  s.ap_enabled = true;
  s.ap_ssid[0] = '\0';  // means cartracker-<vehicle_id>
  copyStr(s.ap_pass, kPassLen, "cartracker");
  s.ap_after_s = 120;
  s.ap_timeout_s = 900;

  copyStr(s.mqtt_host, kStrLen, MQTT_HOST);
  s.mqtt_port = MQTT_PORT;
  copyStr(s.mqtt_user, kStrLen, MQTT_USER);
  copyStr(s.mqtt_pass, kPassLen, MQTT_PASS);
  s.mqtt_tls = true;
  s.mqtt_verify_ca = true;
  copyStr(s.topic_prefix, kStrLen, "cartracker");
  s.mqtt_keepalive = MQTT_KEEPALIVE;

  s.modem_enabled = true;
  copyStr(s.apn, kStrLen, GSM_APN);
  copyStr(s.apn_user, kStrLen, GSM_APN_USER);
  copyStr(s.apn_pass, kPassLen, GSM_APN_PASS);
  copyStr(s.sim_pin, kIdLen, GSM_SIM_PIN);
  s.allow_roaming = true;

  s.pin_gnss_rx = PIN_GNSS_RX;
  s.pin_gnss_tx = PIN_GNSS_TX;
  s.pin_gnss_en = PIN_GNSS_EN;
  s.gnss_baud = GNSS_BAUD;
  s.pin_modem_rx = PIN_MODEM_RX;
  s.pin_modem_tx = PIN_MODEM_TX;
  s.pin_modem_pwrkey = PIN_MODEM_PWRKEY;
  s.pin_modem_en = PIN_MODEM_POWER_EN;
  s.pin_vbat_adc = PIN_VBAT_ADC;
  s.pin_acc_int = PIN_ACC_INT;
  s.pin_i2c_sda = PIN_ACC_SDA;
  s.pin_i2c_scl = PIN_ACC_SCL;
  s.pin_led = PIN_LED;

  copyStr(s.admin_pass, kPassLen, "admin");
  s.portal_enabled = true;
  s.ota_enabled = true;
}

// Is the certificate compiled into config.h a real one, or the placeholder from
// config.example.h?
bool hasBuiltInCa() {
  static const String built_in(MQTT_ROOT_CA);
  return built_in.indexOf("REPLACE_ME") < 0 &&
         built_in.indexOf("BEGIN CERTIFICATE") >= 0;
}

bool validPin(int value, bool allow_none) {
  if (value == -1) return allow_none;
  if (value < 0 || value > 39) return false;
  // 6-11 are the SPI flash lines; driving them bricks the boot.
  if (value >= 6 && value <= 11) return false;
  return true;
}

// Apply one string key from JSON, honouring "leave passwords alone when the
// field comes back empty" so an unchanged form does not wipe a password.
void applyStr(JsonDocument& doc, const char* key, char* dst, size_t len,
              bool keep_when_empty = false) {
  if (!doc[key].is<const char*>()) return;
  const char* value = doc[key];
  if (keep_when_empty && (value == nullptr || value[0] == '\0')) return;
  copyStr(dst, len, value);
}

}  // namespace

void begin() {
  loadDefaults();
  if (!LittleFS.begin(true)) {
    // Not fatal: only the CA certificate lives there.
    Serial.println("settings: LittleFS mount failed");
  }

  prefs.begin(kNamespace, true);
  Settings stored;
  const size_t n = prefs.getBytes("s", &stored, sizeof(stored));
  prefs.end();

  if (n == sizeof(stored) && stored.version == kVersion) {
    s = stored;
  } else if (n != 0) {
    // A struct change invalidates the blob. Defaults are safer than reading
    // fields at the wrong offsets, which would silently corrupt pins and ports.
    Serial.printf("settings: stored blob is %u B / v%u, expected %u B / v%u; "
                  "falling back to defaults\n",
                  static_cast<unsigned>(n), stored.version,
                  static_cast<unsigned>(sizeof(stored)), kVersion);
    save();
  }
}

Settings& get() { return s; }

bool save() {
  prefs.begin(kNamespace, false);
  const size_t written = prefs.putBytes("s", &s, sizeof(s));
  prefs.end();
  return written == sizeof(s);
}

void factoryReset() {
  loadDefaults();
  save();
  if (LittleFS.exists(kCaPath)) LittleFS.remove(kCaPath);
}

String apSsid() {
  if (s.ap_ssid[0] != '\0') return String(s.ap_ssid);
  return String("cartracker-") + s.vehicle_id;
}

bool adminPasswordMatches(const String& candidate) {
  return candidate.length() && candidate == String(s.admin_pass);
}

bool applyJson(const String& json, String& error) {
  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) {
    error = "body is not valid JSON";
    return false;
  }

  Settings next = s;  // validate against a copy, commit only if everything passes
  Settings* p = &next;

  applyStr(doc, "vehicle_id", p->vehicle_id, kIdLen);
  applyStr(doc, "vehicle_name", p->vehicle_name, kStrLen);
  applyStr(doc, "hostname", p->hostname, kStrLen);

  if (doc["wifi_enabled"].is<bool>()) p->wifi_enabled = doc["wifi_enabled"];
  applyStr(doc, "wifi_ssid", p->wifi_ssid, kStrLen);
  applyStr(doc, "wifi_pass", p->wifi_pass, kPassLen, true);
  if (doc["wifi_timeout_s"].is<int>()) p->wifi_timeout_s = doc["wifi_timeout_s"];

  if (doc["ap_enabled"].is<bool>()) p->ap_enabled = doc["ap_enabled"];
  applyStr(doc, "ap_ssid", p->ap_ssid, kStrLen);
  applyStr(doc, "ap_pass", p->ap_pass, kPassLen, true);
  if (doc["ap_after_s"].is<int>()) p->ap_after_s = doc["ap_after_s"];
  if (doc["ap_timeout_s"].is<int>()) p->ap_timeout_s = doc["ap_timeout_s"];

  applyStr(doc, "mqtt_host", p->mqtt_host, kStrLen);
  if (doc["mqtt_port"].is<int>()) p->mqtt_port = doc["mqtt_port"];
  applyStr(doc, "mqtt_user", p->mqtt_user, kStrLen);
  applyStr(doc, "mqtt_pass", p->mqtt_pass, kPassLen, true);
  if (doc["mqtt_tls"].is<bool>()) p->mqtt_tls = doc["mqtt_tls"];
  if (doc["mqtt_verify_ca"].is<bool>()) p->mqtt_verify_ca = doc["mqtt_verify_ca"];
  applyStr(doc, "topic_prefix", p->topic_prefix, kStrLen);
  if (doc["mqtt_keepalive"].is<int>()) p->mqtt_keepalive = doc["mqtt_keepalive"];

  if (doc["modem_enabled"].is<bool>()) p->modem_enabled = doc["modem_enabled"];
  applyStr(doc, "apn", p->apn, kStrLen);
  applyStr(doc, "apn_user", p->apn_user, kStrLen);
  applyStr(doc, "apn_pass", p->apn_pass, kPassLen, true);
  applyStr(doc, "sim_pin", p->sim_pin, kIdLen, true);
  if (doc["allow_roaming"].is<bool>()) p->allow_roaming = doc["allow_roaming"];

  const struct {
    const char* key;
    int8_t* field;
    bool allow_none;
  } pin_fields[] = {
      {"pin_gnss_rx", &p->pin_gnss_rx, false},
      {"pin_gnss_tx", &p->pin_gnss_tx, true},
      {"pin_gnss_en", &p->pin_gnss_en, true},
      {"pin_modem_rx", &p->pin_modem_rx, true},
      {"pin_modem_tx", &p->pin_modem_tx, true},
      {"pin_modem_pwrkey", &p->pin_modem_pwrkey, true},
      {"pin_modem_en", &p->pin_modem_en, true},
      {"pin_vbat_adc", &p->pin_vbat_adc, true},
      {"pin_acc_int", &p->pin_acc_int, true},
      {"pin_i2c_sda", &p->pin_i2c_sda, true},
      {"pin_i2c_scl", &p->pin_i2c_scl, true},
      {"pin_led", &p->pin_led, true},
  };
  for (const auto& f : pin_fields) {
    if (!doc[f.key].is<int>()) continue;
    const int value = doc[f.key];
    if (!validPin(value, f.allow_none)) {
      error = String(f.key) + ": GPIO " + value +
              " is not usable (6-11 drive the SPI flash, range is 0-39)";
      return false;
    }
    *f.field = static_cast<int8_t>(value);
  }
  if (doc["gnss_baud"].is<uint32_t>()) p->gnss_baud = doc["gnss_baud"];

  applyStr(doc, "admin_pass", p->admin_pass, kPassLen, true);
  if (doc["portal_enabled"].is<bool>()) p->portal_enabled = doc["portal_enabled"];
  if (doc["ota_enabled"].is<bool>()) p->ota_enabled = doc["ota_enabled"];

  // --- cross-field rules, all refusals rather than silent corrections ---
  if (p->vehicle_id[0] == '\0') {
    error = "vehicle_id must not be empty, it forms the MQTT topic";
    return false;
  }
  for (const char* c = p->vehicle_id; *c; c++) {
    if (!isalnum(*c) && *c != '-' && *c != '_') {
      error = "vehicle_id accepts letters, digits, '-' and '_' only";
      return false;
    }
  }
  if (p->mqtt_host[0] == '\0') {
    error = "mqtt_host must not be empty";
    return false;
  }
  if (p->mqtt_port == 0) {
    error = "mqtt_port must be 1-65535";
    return false;
  }
  if (p->ap_pass[0] != '\0' && strlen(p->ap_pass) < 8) {
    // WPA2 refuses shorter keys; the AP would silently come up open instead.
    error = "ap_pass must be at least 8 characters, or empty for an open AP";
    return false;
  }
  if (p->admin_pass[0] == '\0') {
    error = "admin_pass must not be empty";
    return false;
  }
  if (p->pin_gnss_rx == p->pin_gnss_tx) {
    error = "GNSS RX and TX cannot be the same pin";
    return false;
  }
  if (p->mqtt_tls && p->mqtt_verify_ca && !hasCaCert() && !hasBuiltInCa()) {
    // Only a problem when there is no certificate at all. The firmware ships a
    // compiled-in CA as a fallback, and refusing the save while that one is
    // present would block every unrelated setting for no reason.
    error = "CA verification is on, no certificate is stored and the firmware "
            "has no built-in one; upload a PEM or turn verification off";
    return false;
  }

  s = next;
  if (!save()) {
    error = "NVS write failed";
    return false;
  }
  return true;
}

String toJson() {
  JsonDocument doc;
  doc["vehicle_id"] = s.vehicle_id;
  doc["vehicle_name"] = s.vehicle_name;
  doc["hostname"] = s.hostname;

  doc["wifi_enabled"] = s.wifi_enabled;
  doc["wifi_ssid"] = s.wifi_ssid;
  doc["wifi_pass_set"] = s.wifi_pass[0] != '\0';
  doc["wifi_timeout_s"] = s.wifi_timeout_s;

  doc["ap_enabled"] = s.ap_enabled;
  doc["ap_ssid"] = s.ap_ssid;
  doc["ap_ssid_effective"] = apSsid();
  doc["ap_pass_set"] = s.ap_pass[0] != '\0';
  doc["ap_after_s"] = s.ap_after_s;
  doc["ap_timeout_s"] = s.ap_timeout_s;

  doc["mqtt_host"] = s.mqtt_host;
  doc["mqtt_port"] = s.mqtt_port;
  doc["mqtt_user"] = s.mqtt_user;
  doc["mqtt_pass_set"] = s.mqtt_pass[0] != '\0';
  doc["mqtt_tls"] = s.mqtt_tls;
  doc["mqtt_verify_ca"] = s.mqtt_verify_ca;
  doc["topic_prefix"] = s.topic_prefix;
  doc["mqtt_keepalive"] = s.mqtt_keepalive;
  doc["ca_present"] = hasCaCert();

  doc["modem_enabled"] = s.modem_enabled;
  doc["apn"] = s.apn;
  doc["apn_user"] = s.apn_user;
  doc["apn_pass_set"] = s.apn_pass[0] != '\0';
  doc["sim_pin_set"] = s.sim_pin[0] != '\0';
  doc["allow_roaming"] = s.allow_roaming;

  doc["pin_gnss_rx"] = s.pin_gnss_rx;
  doc["pin_gnss_tx"] = s.pin_gnss_tx;
  doc["pin_gnss_en"] = s.pin_gnss_en;
  doc["gnss_baud"] = s.gnss_baud;
  doc["pin_modem_rx"] = s.pin_modem_rx;
  doc["pin_modem_tx"] = s.pin_modem_tx;
  doc["pin_modem_pwrkey"] = s.pin_modem_pwrkey;
  doc["pin_modem_en"] = s.pin_modem_en;
  doc["pin_vbat_adc"] = s.pin_vbat_adc;
  doc["pin_acc_int"] = s.pin_acc_int;
  doc["pin_i2c_sda"] = s.pin_i2c_sda;
  doc["pin_i2c_scl"] = s.pin_i2c_scl;
  doc["pin_led"] = s.pin_led;

  doc["admin_pass_set"] = s.admin_pass[0] != '\0';
  doc["portal_enabled"] = s.portal_enabled;
  doc["ota_enabled"] = s.ota_enabled;

  String out;
  serializeJson(doc, out);
  return out;
}

String caCert() {
  if (!LittleFS.exists(kCaPath)) return String();
  File f = LittleFS.open(kCaPath, "r");
  if (!f) return String();
  String pem = f.readString();
  f.close();
  return pem;
}

bool hasCaCert() {
  return LittleFS.exists(kCaPath) && LittleFS.open(kCaPath, "r").size() > 100;
}

bool setCaCert(const String& pem, String& error) {
  if (pem.length() == 0) {
    if (LittleFS.exists(kCaPath)) LittleFS.remove(kCaPath);
    return true;
  }
  // Cheap sanity check. A truncated paste is the common failure and it would
  // otherwise surface much later as an unexplained TLS handshake error.
  if (pem.indexOf("-----BEGIN CERTIFICATE-----") < 0 ||
      pem.indexOf("-----END CERTIFICATE-----") < 0) {
    error = "not a PEM certificate (missing BEGIN/END CERTIFICATE lines)";
    return false;
  }
  if (pem.length() > 8192) {
    error = "certificate larger than 8 kB";
    return false;
  }
  File f = LittleFS.open(kCaPath, "w");
  if (!f) {
    error = "cannot open " + String(kCaPath) + " for writing";
    return false;
  }
  const size_t written = f.print(pem);
  f.close();
  if (written != pem.length()) {
    error = "short write to LittleFS";
    return false;
  }
  return true;
}

}  // namespace settings
