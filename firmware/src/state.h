// Shared types. The single source of truth about what the vehicle is doing lives
// in main.cpp; lower layers report facts into these structs and never decide
// anything on their own (docs/02 section 2.2).
#pragma once
#include <Arduino.h>

enum VehicleMode : uint8_t {
  MODE_PARKED = 0,
  MODE_DRIVING = 1,
  MODE_MOVED = 2,  // movement while parked: towing or theft
  MODE_HIBERNATE = 3,
};

inline const char* modeName(VehicleMode m) {
  switch (m) {
    case MODE_DRIVING: return "driving";
    case MODE_MOVED: return "moved";
    case MODE_HIBERNATE: return "hibernate";
    default: return "parked";
  }
}

enum TsSource : uint8_t { TS_NONE = 0, TS_GNSS = 1, TS_NTP = 2 };

// One position sample. Kept flat and POD so it can be written to the offline
// queue byte for byte (docs/02 section 2.6).
struct __attribute__((packed)) PosRecord {
  uint32_t seq;
  uint32_t ts;      // UTC unix seconds, 0 = unknown
  int32_t lat_e7;   // degrees * 1e7, integer to keep the record fixed size
  int32_t lon_e7;
  int16_t alt_m;
  uint16_t spd_ckmh;  // km/h * 100
  uint16_t crs_deg;   // 0..359
  uint8_t sat;
  uint8_t hdop_x10;
  uint8_t fix;       // 0 none, 2 = 2D, 3 = 3D
  uint8_t mode;      // VehicleMode
  uint8_t ts_src;    // TsSource
  uint8_t src;       // 0 = external NEO-6M, 1 = modem GNSS
  uint8_t _pad[2];
};
static_assert(sizeof(PosRecord) == 30, "PosRecord layout changed, bump STORE_VERSION");

struct Telemetry {
  float vbat = 0.0f;
  float vsys = 0.0f;
  float temp = NAN;
  int16_t rssi = 0;
  bool roaming = false;
  char net[8] = "";
  char oper[12] = "";
  uint32_t uptime_s = 0;
  uint16_t queued = 0;
  uint8_t reset_reason = 0;
};

// Runtime configuration. Defaults match docs/05 section 5.6 and are overwritten
// by the retained cfg topic, then persisted to NVS.
struct Config {
  uint16_t int_drive = 30;
  uint16_t int_park = 3600;
  uint16_t int_alarm = 15;
  float v_drive_on = 13.2f;
  float v_drive_off = 13.0f;
  float v_warn = 12.2f;
  float v_hib = 11.9f;
  float v_wake = 12.4f;
  uint16_t crs_delta = 25;
  float hdop_max = 3.0f;
  uint8_t motion_sens = 3;   // 1 = least sensitive, 5 = most
  char gnss_src[8] = "auto";  // neo6m | modem | auto
};
