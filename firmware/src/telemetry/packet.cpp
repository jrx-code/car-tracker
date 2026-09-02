#include "packet.h"

#include <ArduinoJson.h>

namespace packet {
namespace {

const char* tsSourceName(uint8_t s) {
  switch (s) {
    case TS_GNSS: return "gnss";
    case TS_NTP: return "ntp";
    default: return "none";
  }
}

void fillPos(JsonObject o, const PosRecord& rec, bool compact) {
  if (compact) {
    o["q"] = rec.seq;
    o["t"] = rec.ts;
    o["a"] = rec.lat_e7 / 1e7;
    o["o"] = rec.lon_e7 / 1e7;
    o["s"] = rec.spd_ckmh / 100.0;
    o["c"] = rec.crs_deg;
    o["n"] = rec.sat;
    o["h"] = rec.hdop_x10 / 10.0;
    o["m"] = rec.mode;
    o["r"] = rec.src;
    return;
  }
  o["seq"] = rec.seq;
  o["ts"] = rec.ts;
  o["ts_src"] = tsSourceName(rec.ts_src);
  o["lat"] = rec.lat_e7 / 1e7;
  o["lon"] = rec.lon_e7 / 1e7;
  o["alt"] = rec.alt_m;
  o["spd"] = rec.spd_ckmh / 100.0;
  o["crs"] = rec.crs_deg;
  o["sat"] = rec.sat;
  o["hdop"] = rec.hdop_x10 / 10.0;
  o["fix"] = rec.fix;
  o["st"] = modeName(static_cast<VehicleMode>(rec.mode));
  o["src"] = rec.src == 1 ? "modem" : "neo6m";
}

}  // namespace

size_t buildPos(const PosRecord& rec, char* out, size_t out_len, bool compact) {
  JsonDocument doc;
  fillPos(doc.to<JsonObject>(), rec, compact);
  return serializeJson(doc, out, out_len);
}

size_t buildTel(const Telemetry& tel, VehicleMode mode, uint32_t seq, uint32_t ts,
                const char* ip, char* out, size_t out_len) {
  JsonDocument doc;
  doc["seq"] = seq;
  doc["ts"] = ts;
  doc["vbat"] = roundf(tel.vbat * 100) / 100.0;
  doc["vsys"] = roundf(tel.vsys * 100) / 100.0;
  if (!isnan(tel.temp)) doc["temp"] = roundf(tel.temp * 10) / 10.0;
  doc["rssi"] = tel.rssi;
  doc["net"] = tel.net;
  doc["op"] = tel.oper;
  doc["roam"] = tel.roaming;
  doc["up"] = tel.uptime_s;
  doc["q"] = tel.queued;
  doc["rst"] = tel.reset_reason;
  doc["st"] = modeName(mode);
  // Refreshed with every telemetry packet: a DHCP lease can move the device.
  if (ip && ip[0] && strcmp(ip, "0.0.0.0") != 0) doc["ip"] = ip;
  return serializeJson(doc, out, out_len);
}

size_t buildEvent(const char* ev, uint32_t seq, uint32_t ts, const PosRecord* pos,
                  char* out, size_t out_len) {
  JsonDocument doc;
  doc["seq"] = seq;
  doc["ts"] = ts;
  doc["ev"] = ev;
  if (pos != nullptr) {
    doc["lat"] = pos->lat_e7 / 1e7;
    doc["lon"] = pos->lon_e7 / 1e7;
  }
  return serializeJson(doc, out, out_len);
}

size_t buildInfo(const transport::LinkInfo& link, const char* fw_version,
                 const char* modem_name, const char* ip, char* out, size_t out_len) {
  JsonDocument doc;
  doc["fw"] = fw_version;
  doc["modem"] = modem_name;
  doc["imei"] = link.imei;
  doc["iccid"] = link.iccid;
  doc["net"] = link.net;
  if (ip && ip[0]) doc["ip"] = ip;
  return serializeJson(doc, out, out_len);
}

size_t buildBatch(const PosRecord* recs, uint16_t n, char* out, size_t out_len,
                  bool compact) {
  JsonDocument doc;
  doc["n"] = n;
  JsonArray arr = doc["pts"].to<JsonArray>();
  for (uint16_t i = 0; i < n; i++) {
    fillPos(arr.add<JsonObject>(), recs[i], compact);
  }
  return serializeJson(doc, out, out_len);
}

size_t buildAck(const char* id, bool ok, uint32_t ms, const char* msg, char* out,
                size_t out_len) {
  JsonDocument doc;
  doc["id"] = id;
  doc["ok"] = ok;
  doc["ms"] = ms;
  doc["msg"] = msg;
  return serializeJson(doc, out, out_len);
}

bool applyConfig(const uint8_t* payload, unsigned len, Config& cfg) {
  JsonDocument doc;
  if (deserializeJson(doc, payload, len) != DeserializationError::Ok) return false;

  cfg.int_drive = doc["int_drive"] | cfg.int_drive;
  cfg.int_park = doc["int_park"] | cfg.int_park;
  cfg.int_alarm = doc["int_alarm"] | cfg.int_alarm;
  cfg.v_drive_on = doc["v_drive_on"] | cfg.v_drive_on;
  cfg.v_drive_off = doc["v_drive_off"] | cfg.v_drive_off;
  cfg.v_warn = doc["v_warn"] | cfg.v_warn;
  cfg.v_hib = doc["v_hib"] | cfg.v_hib;
  cfg.v_wake = doc["v_wake"] | cfg.v_wake;
  cfg.crs_delta = doc["crs_delta"] | cfg.crs_delta;
  cfg.hdop_max = doc["hdop_max"] | cfg.hdop_max;
  cfg.motion_sens = doc["motion_sens"] | cfg.motion_sens;
  if (doc["gnss_src"].is<const char*>()) {
    strncpy(cfg.gnss_src, doc["gnss_src"], sizeof(cfg.gnss_src) - 1);
  }

  // Guard rails: a bad cfg must not be able to brick the device in the field.
  // Hibernation below 11.0 V would let the tracker flatten the car battery,
  // which is exactly what assumption Z2 forbids.
  if (cfg.int_drive < 5) cfg.int_drive = 5;
  if (cfg.int_park < 60) cfg.int_park = 60;
  if (cfg.v_hib < 11.0f) cfg.v_hib = 11.0f;
  if (cfg.v_wake <= cfg.v_hib) cfg.v_wake = cfg.v_hib + 0.4f;
  if (cfg.v_drive_off >= cfg.v_drive_on) cfg.v_drive_off = cfg.v_drive_on - 0.2f;
  return true;
}

}  // namespace packet
