#include "gnss.h"

#include <TinyGPSPlus.h>

#include "pins.h"
#include "power/power.h"
#include "util/timeutil.h"

namespace gnss {
namespace {

TinyGPSPlus gps;
HardwareSerial uart(1);
bool is_enabled = false;
uint32_t last_fix_ms = 0;

// Last fix reported by the modem receiver, kept separately so that the two
// sources never overwrite each other silently.
struct ModemFix {
  bool valid = false;
  double lat = 0, lon = 0;
  float spd = 0, crs = 0, alt = 0, hdop = 99;
  int sats = 0;
  uint32_t ts = 0;
  uint32_t at_ms = 0;
} modem_fix;

// Non-const references: the TinyGPSPlus accessors are not const methods.
uint32_t toUnix(TinyGPSDate& d, TinyGPSTime& t) {
  if (!d.isValid() || !t.isValid()) return 0;
  // NMEA time is UTC, so it must not go through mktime(), which is local time.
  return timeutil::toUnixUtc(d.year(), d.month(), d.day(), t.hour(), t.minute(),
                             t.second());
}

}  // namespace

void begin() {
  uart.begin(GNSS_BAUD, SERIAL_8N1, PIN_GNSS_RX, PIN_GNSS_TX);
}

void enable() {
  if (is_enabled) return;
  power::gnssPower(true);
  uart.begin(GNSS_BAUD, SERIAL_8N1, PIN_GNSS_RX, PIN_GNSS_TX);
  is_enabled = true;
}

void disable() {
  if (!is_enabled) return;
  uart.end();
  power::gnssPower(false);
  is_enabled = false;
}

bool enabled() { return is_enabled; }

void poll() {
  while (uart.available()) {
    if (gps.encode(uart.read()) && gps.location.isValid() &&
        gps.location.isUpdated()) {
      last_fix_ms = millis();
    }
  }
}

bool hasFix() { return gps.location.isValid() && gps.satellites.value() >= 4; }
uint8_t satellites() { return static_cast<uint8_t>(gps.satellites.value()); }
float hdop() { return gps.hdop.isValid() ? gps.hdop.hdop() : 99.0f; }
uint32_t utc() { return toUnix(gps.date, gps.time); }
uint32_t msSinceFix() { return last_fix_ms ? (millis() - last_fix_ms) : UINT32_MAX; }

void feedModemFix(double lat, double lon, float speed_kmh, float course,
                  float alt, int sats, float h, uint32_t utc_ts) {
  modem_fix = {true, lat, lon, speed_kmh, course, alt, h, sats, utc_ts, millis()};
}

bool fill(PosRecord& out, float hdop_max) {
  const bool uart_ok = hasFix() && hdop() <= hdop_max && msSinceFix() < 10000;
  const bool modem_ok = modem_fix.valid && modem_fix.hdop <= hdop_max &&
                        (millis() - modem_fix.at_ms) < 10000;

  // With both available, take the better HDOP. That is what gnss_src "auto"
  // means, and it is also how we find out in the field which receiver is
  // actually better in this car (docs/05 section 5.2, field src).
  const bool use_modem = modem_ok && (!uart_ok || modem_fix.hdop < hdop());
  if (!uart_ok && !modem_ok) return false;

  if (use_modem) {
    out.lat_e7 = static_cast<int32_t>(modem_fix.lat * 1e7);
    out.lon_e7 = static_cast<int32_t>(modem_fix.lon * 1e7);
    out.alt_m = static_cast<int16_t>(modem_fix.alt);
    out.spd_ckmh = static_cast<uint16_t>(modem_fix.spd * 100.0f);
    out.crs_deg = static_cast<uint16_t>(modem_fix.crs) % 360;
    out.sat = static_cast<uint8_t>(modem_fix.sats);
    out.hdop_x10 = static_cast<uint8_t>(min(25.5f, modem_fix.hdop) * 10);
    out.fix = 3;
    out.ts = modem_fix.ts;
    out.ts_src = modem_fix.ts ? TS_GNSS : TS_NONE;
    out.src = 1;
  } else {
    out.lat_e7 = static_cast<int32_t>(gps.location.lat() * 1e7);
    out.lon_e7 = static_cast<int32_t>(gps.location.lng() * 1e7);
    out.alt_m = static_cast<int16_t>(gps.altitude.isValid() ? gps.altitude.meters() : 0);
    out.spd_ckmh = static_cast<uint16_t>(
        (gps.speed.isValid() ? gps.speed.kmph() : 0.0f) * 100.0f);
    out.crs_deg = static_cast<uint16_t>(
        gps.course.isValid() ? gps.course.deg() : 0.0f) % 360;
    out.sat = satellites();
    out.hdop_x10 = static_cast<uint8_t>(min(25.5f, hdop()) * 10);
    out.fix = 3;
    const uint32_t t = utc();
    out.ts = t;
    out.ts_src = t ? TS_GNSS : TS_NONE;
    out.src = 0;
  }
  return true;
}

}  // namespace gnss
