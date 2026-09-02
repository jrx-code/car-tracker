// car-tracker: OBD powered GPS/LTE tracker for two Mazda MX-5 ND.
// State machine and the single source of truth about the vehicle state.
// Design: docs/02-architektura.md. Wire format: docs/05-protokol-mqtt.md.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>

#include "config.h"
#include "gnss/gnss.h"
#include "modem/transport.h"
#include "pins.h"
#include "portal/portal.h"
#include "power/motion.h"
#include "power/power.h"
#include "settings/settings.h"
#include "state.h"
#include "telemetry/packet.h"
#include "telemetry/store.h"

namespace {

constexpr const char* kFwVersion = "0.1.0";

#if defined(MODEM_PROFILE_SIM7670G)
constexpr const char* kModemName = "SIM7670G";
#elif defined(MODEM_PROFILE_A7670)
constexpr const char* kModemName = "A7670";
#elif defined(MODEM_PROFILE_SIM7080G)
constexpr const char* kModemName = "SIM7080G";
#elif defined(MODEM_PROFILE_SIM7600E)
constexpr const char* kModemName = "SIM7600E-H";
#else
constexpr const char* kModemName = "none-wifi";
#endif

// Survives deep sleep, unlike anything in the normal heap.
RTC_DATA_ATTR VehicleMode rtc_mode = MODE_PARKED;
RTC_DATA_ATTR uint32_t rtc_seq = 0;
RTC_DATA_ATTR uint16_t rtc_last_course = 0;
RTC_DATA_ATTR uint32_t rtc_boot_count = 0;

Preferences prefs;
Config cfg;
Telemetry tel;
char vehicle_id[12] = DEFAULT_VEHICLE_ID;

char t_status[48], t_info[48], t_pos[48], t_tel[48], t_evt[48], t_batch[48];
char t_cfg[48], t_cmd[48], t_ack[48];
char buf[2048];

uint32_t last_pos_ms = 0;
uint32_t last_tel_ms = 0;
uint32_t mode_since_ms = 0;
uint32_t voltage_low_since_ms = 0;
uint32_t drive_hint_since_ms = 0;
bool warned_low_battery = false;
PosRecord last_pos = {};
bool have_last_pos = false;

void buildTopics() {
  const char* prefix = settings::get().topic_prefix;
  snprintf(t_status, sizeof(t_status), "%s/%s/status", prefix, vehicle_id);
  snprintf(t_info, sizeof(t_info), "%s/%s/info", prefix, vehicle_id);
  snprintf(t_pos, sizeof(t_pos), "%s/%s/pos", prefix, vehicle_id);
  snprintf(t_tel, sizeof(t_tel), "%s/%s/tel", prefix, vehicle_id);
  snprintf(t_evt, sizeof(t_evt), "%s/%s/evt", prefix, vehicle_id);
  snprintf(t_batch, sizeof(t_batch), "%s/%s/batch", prefix, vehicle_id);
  snprintf(t_cfg, sizeof(t_cfg), "%s/%s/cfg", prefix, vehicle_id);
  snprintf(t_cmd, sizeof(t_cmd), "%s/%s/cmd", prefix, vehicle_id);
  snprintf(t_ack, sizeof(t_ack), "%s/%s/ack", prefix, vehicle_id);
}

void loadNvs() {
  // Identity lives in the settings module now, so the portal and the firmware
  // cannot disagree about which vehicle this is.
  strncpy(vehicle_id, settings::get().vehicle_id, sizeof(vehicle_id) - 1);
  vehicle_id[sizeof(vehicle_id) - 1] = '\0';

  prefs.begin("tracker", true);
  rtc_seq = max(rtc_seq, prefs.getUInt("seq", 0));
  size_t n = prefs.getBytes("cfg", &cfg, sizeof(cfg));
  if (n != sizeof(cfg)) cfg = Config();  // struct changed, fall back to defaults
  prefs.end();
}

void saveCfg() {
  prefs.begin("tracker", false);
  prefs.putBytes("cfg", &cfg, sizeof(cfg));
  prefs.end();
}

void saveSeq() {
  // Written every 16 sequence numbers, not every one: NVS has a finite erase
  // budget and a gap in seq after a power cut is harmless, a worn flash is not.
  if ((rtc_seq & 0x0F) != 0) return;
  prefs.begin("tracker", false);
  prefs.putUInt("seq", rtc_seq);
  prefs.end();
}

uint32_t nowTs() {
  const uint32_t g = gnss::utc();
  if (g) return g;
  return transport::networkTime();
}

void publishEvent(const char* ev) {
  const size_t n = packet::buildEvent(ev, ++rtc_seq, nowTs(),
                                      have_last_pos ? &last_pos : nullptr, buf,
                                      sizeof(buf));
  if (n) transport::publish(t_evt, buf, false);
  saveSeq();
}

void setMode(VehicleMode m, const char* event) {
  if (rtc_mode == m) return;
  rtc_mode = m;
  mode_since_ms = millis();
  if (event) publishEvent(event);
}

// Two independent sources, because neither alone is reliable: voltage lies
// during i-stop, the accelerometer lies when a door is slammed (docs/02 2.4).
void updateMode(float vbat) {
  const uint32_t now = millis();
  const bool moving = motion::sustainedMotion(10000);

  // No divider wired, so no voltage to reason about. On the bench and in the
  // PoC the board runs off USB or a power bank, and treating the resulting
  // near-zero reading as a flat car battery would put the tracker into
  // hibernation within ten minutes of every power-up.
  const bool voltage_available = vbat > 1.0f;
  if (!voltage_available) {
    // Fall back to movement alone: drive when the accelerometer says so, park
    // when it has been still for two minutes.
    if (rtc_mode == MODE_DRIVING) {
      if (!moving) {
        if (drive_hint_since_ms == 0) drive_hint_since_ms = now;
        if (now - drive_hint_since_ms > 120000UL) {
          drive_hint_since_ms = 0;
          setMode(MODE_PARKED, "trip_end");
        }
      } else {
        drive_hint_since_ms = 0;
      }
    } else if (moving) {
      setMode(MODE_DRIVING, "trip_start");
    }
    return;
  }

  const bool voltage_says_running = vbat >= cfg.v_drive_on;

  if (rtc_mode == MODE_HIBERNATE) {
    if (vbat >= cfg.v_wake) setMode(MODE_PARKED, "wakeup");
    return;
  }

  if (vbat <= cfg.v_hib) {
    if (voltage_low_since_ms == 0) voltage_low_since_ms = now;
    if (now - voltage_low_since_ms > 10UL * 60UL * 1000UL) {
      setMode(MODE_HIBERNATE, "hibernate");
      return;
    }
  } else {
    voltage_low_since_ms = 0;
  }

  if (vbat <= cfg.v_warn && !warned_low_battery) {
    warned_low_battery = true;
    publishEvent("battery_low");
  } else if (vbat > cfg.v_warn + 0.2f) {
    warned_low_battery = false;
  }

  if (rtc_mode == MODE_DRIVING) {
    const bool stopped = (vbat < cfg.v_drive_off) && !moving;
    if (stopped) {
      if (drive_hint_since_ms == 0) drive_hint_since_ms = now;
      if (now - drive_hint_since_ms > 120000UL) {
        drive_hint_since_ms = 0;
        setMode(MODE_PARKED, "trip_end");
      }
    } else {
      drive_hint_since_ms = 0;
    }
    return;
  }

  // PARKED or MOVED
  if (voltage_says_running) {
    if (drive_hint_since_ms == 0) drive_hint_since_ms = now;
    if (now - drive_hint_since_ms > 5000UL) {
      drive_hint_since_ms = 0;
      setMode(MODE_DRIVING, "trip_start");
    }
    return;
  }
  drive_hint_since_ms = 0;

  // Movement without the engine running: towing or theft. This decision is made
  // in firmware and not in HA on purpose, it has to work with HA down (docs/02 2.8).
  if (moving && rtc_mode == MODE_PARKED) {
    setMode(MODE_MOVED, "motion_alarm");
  } else if (!moving && rtc_mode == MODE_MOVED &&
             millis() - mode_since_ms > 5UL * 60UL * 1000UL) {
    setMode(MODE_PARKED, nullptr);
  }
}

uint16_t posIntervalMs() {
  switch (rtc_mode) {
    case MODE_MOVED: return cfg.int_alarm * 1000;
    case MODE_DRIVING: return cfg.int_drive * 1000;
    default: return 0;  // no positions while parked
  }
}

bool courseChangedEnough(const PosRecord& rec) {
  if (!have_last_pos) return true;
  int diff = abs(static_cast<int>(rec.crs_deg) - static_cast<int>(rtc_last_course));
  if (diff > 180) diff = 360 - diff;
  return diff >= static_cast<int>(cfg.crs_delta) && rec.spd_ckmh > 500;
}

void sendOrQueue(const PosRecord& rec) {
  const size_t n = packet::buildPos(rec, buf, sizeof(buf), false);
  if (!n) return;
  if (transport::connected() && transport::publish(t_pos, buf, false)) return;
  store::push(rec);  // no link: keep it, the backlog is flushed later
}

void flushBacklog() {
  static PosRecord batch[store::kBatchMax];
  while (transport::connected() && store::count() > 0) {
    const uint16_t n = store::peek(batch, store::kBatchMax);
    if (n == 0) return;
    const size_t len = packet::buildBatch(batch, n, buf, sizeof(buf), true);
    if (len == 0 || len >= sizeof(buf)) {
      // Payload would not fit: halve the batch rather than spin forever.
      const size_t half = packet::buildBatch(batch, n / 2, buf, sizeof(buf), true);
      if (half == 0) return;
      if (!transport::publish(t_batch, buf, false)) return;
      store::drop(n / 2);
      continue;
    }
    // Only PUBACK drops the records, so a failure here duplicates rather than loses.
    if (!transport::publish(t_batch, buf, false)) return;
    store::drop(n);
    transport::loop();
  }
}

void publishTelemetry() {
  const transport::LinkInfo& link = transport::info();
  tel.vbat = power::readVbat();
  tel.vsys = power::readVsys();
  tel.rssi = link.rssi;
  tel.roaming = link.roaming;
  strncpy(tel.net, link.net, sizeof(tel.net) - 1);
  strncpy(tel.oper, link.oper, sizeof(tel.oper) - 1);
  tel.uptime_s = millis() / 1000;
  tel.queued = store::count();
  tel.reset_reason = power::resetReason();

  const size_t n = packet::buildTel(tel, rtc_mode, ++rtc_seq, nowTs(),
                                    portal::ipAddress().c_str(), buf, sizeof(buf));
  if (n) transport::publish(t_tel, buf, false);
  saveSeq();
}

void handleCommand(const uint8_t* payload, unsigned len) {
  JsonDocument doc;
  if (deserializeJson(doc, payload, len) != DeserializationError::Ok) return;
  const char* id = doc["id"] | "";
  const char* cmd = doc["cmd"] | "";
  const uint32_t t0 = millis();

  if (strcmp(cmd, "ping") == 0) {
    packet::buildAck(id, true, millis() - t0, "", buf, sizeof(buf));
    transport::publish(t_ack, buf, false);
  } else if (strcmp(cmd, "locate") == 0) {
    gnss::enable();
    PosRecord rec = {};
    const uint32_t deadline = millis() + 60000;
    bool got = false;
    while (millis() < deadline) {
      gnss::poll();
      if (gnss::fill(rec, cfg.hdop_max)) { got = true; break; }
      delay(100);
    }
    if (got) {
      rec.seq = ++rtc_seq;
      rec.mode = rtc_mode;
      sendOrQueue(rec);
    }
    packet::buildAck(id, got, millis() - t0, got ? "" : "no fix", buf, sizeof(buf));
    transport::publish(t_ack, buf, false);
  } else if (strcmp(cmd, "reboot") == 0) {
    packet::buildAck(id, true, millis() - t0, "rebooting", buf, sizeof(buf));
    transport::publish(t_ack, buf, false);
    delay(500);
    ESP.restart();
  } else if (strcmp(cmd, "set_id") == 0) {
    const char* nid = doc["vehicle_id"] | "";
    bool ok = doc["confirm"].as<bool>() && strlen(nid) > 0 && strlen(nid) < 12;
    String err = "refused";
    if (ok) {
      char body[64];
      snprintf(body, sizeof(body), "{\"vehicle_id\":\"%s\"}", nid);
      ok = settings::applyJson(String(body), err);
    }
    packet::buildAck(id, ok, millis() - t0,
                     ok ? "restart required" : err.c_str(), buf, sizeof(buf));
    transport::publish(t_ack, buf, false);
  } else if (strcmp(cmd, "ota") == 0) {
    // Deliberately not implemented over LTE: a firmware image over a metered
    // link, in a moving car, is a good way to end up with a bricked tracker in
    // a parking garage. OTA runs over WiFi in the garage only (docs/07).
    packet::buildAck(id, false, millis() - t0, "ota over wifi only", buf, sizeof(buf));
    transport::publish(t_ack, buf, false);
  }
}

void onMessage(const char* topic, const uint8_t* payload, unsigned len) {
  if (strcmp(topic, t_cfg) == 0) {
    if (packet::applyConfig(payload, len, cfg)) {
      saveCfg();
      motion::setSensitivity(cfg.motion_sens);
    }
  } else if (strcmp(topic, t_cmd) == 0) {
    handleCommand(payload, len);
  }
}

bool connectAndAnnounce() {
  char client_id[32];
  snprintf(client_id, sizeof(client_id), "cartracker-%s", vehicle_id);
  if (!transport::connect(client_id, MQTT_USER, MQTT_PASS, t_status, "offline")) {
    return false;
  }
  transport::publish(t_status, "online", true);
  const size_t n = packet::buildInfo(transport::info(), settings::get(), kFwVersion,
                                     kModemName, portal::ipAddress().c_str(), buf,
                                     sizeof(buf));
  if (n) transport::publish(t_info, buf, true);
  transport::subscribe(t_cfg);
  transport::subscribe(t_cmd);
  return true;
}

// Parked: nothing to send except the hourly telemetry, so cut both rails and
// sleep. This is where the current budget of docs/04 is actually earned.
void parkedSleep() {
  // Never sleep while somebody is using the emergency AP: the whole point of
  // that AP is to be reachable, and a device that sleeps mid-configuration is
  // exactly the situation it exists to prevent.
  if (portal::apActive()) {
    delay(200);
    return;
  }
  // No voltage divider means bench or power bank. Sleeping there cuts the
  // portal off and, on a power bank, the low draw makes the bank shut down.
  if (power::readVbat() <= 1.0f) {
    delay(200);
    return;
  }
  const uint32_t sleep_s =
      (rtc_mode == MODE_HIBERNATE) ? 6UL * 3600UL : cfg.int_park;
  transport::publish(t_status, "offline", true);
  transport::loop();
  delay(100);
  transport::sleep();
  gnss::disable();
  power::deepSleep(sleep_s, rtc_mode != MODE_HIBERNATE);
}

}  // namespace

// Snapshot for the portal status page. Kept here because main.cpp is the only
// place that knows the vehicle state (docs/02 section 2.2).
String statusJson() {
  JsonDocument doc;
  doc["fw"] = kFwVersion;
  doc["mode"] = modeName(rtc_mode);
  doc["vehicle_id"] = vehicle_id;
  doc["mqtt"] = transport::connected();
  doc["queued"] = store::count();
  doc["uptime"] = millis() / 1000;
  doc["sat"] = gnss::satellites();
  doc["fix"] = gnss::hasFix();
  if (gnss::hasFix()) {
    doc["hdop"] = gnss::hdop();
    // Straight from the receiver, not from the last transmitted point: while
    // parked nothing is transmitted, so the buffered position stays empty and
    // the page would claim "no position" next to a healthy satellite count.
    PosRecord now = {};
    if (gnss::fill(now, 99.0f)) {
      doc["lat"] = now.lat_e7 / 1e7;
      doc["lon"] = now.lon_e7 / 1e7;
      doc["speed"] = now.spd_ckmh / 100.0;
    }
  }
  const float vbat = power::readVbat();
  if (!isnan(vbat) && vbat > 1.0f) doc["vbat"] = vbat;
  String out;
  serializeJson(doc, out);
  return out;
}

void setup() {
  Serial.begin(115200);
  rtc_boot_count++;

  settings::begin();
  power::begin();
  loadNvs();
  buildTopics();

  portal::setStatusProvider(statusJson);
  portal::begin();

  store::begin();
  gnss::begin();
  motion::begin(cfg.motion_sens);
  transport::begin();
  transport::onMessage(onMessage);

  const float vbat = power::readVbat();

  // Woken by the accelerometer while parked: this is the alarm path, and it has
  // to be fast, so the modem comes up before anything else is decided.
  const bool woke_on_motion =
      (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0);
  if (woke_on_motion && vbat < cfg.v_drive_on) {
    rtc_mode = MODE_MOVED;
    gnss::enable();
  }

  if (rtc_mode == MODE_HIBERNATE && vbat < cfg.v_wake) {
    // Still flat. One telemetry packet costs less than staying awake.
    if (connectAndAnnounce()) publishTelemetry();
    parkedSleep();
  }

  if (rtc_mode == MODE_DRIVING || rtc_mode == MODE_MOVED) gnss::enable();
  connectAndAnnounce();
  publishTelemetry();
  flushBacklog();
  mode_since_ms = millis();
}

void loop() {
  portal::loop();
  gnss::poll();
  transport::loop();

  const float vbat = power::readVbat();
  updateMode(vbat);

  if (!transport::connected()) {
    // A failing TLS handshake is slow and noisy; back off instead of hammering.
    static uint32_t last_retry = 0;
    static uint16_t retry_gap_s = 15;
    // The first attempt happens before the portal has finished joining WiFi, so
    // it always fails and would otherwise push the backoff to minutes before the
    // link is even up. Reset it the moment the network appears.
    static bool had_link = false;
    const bool link = portal::staConnected();
    if (link && !had_link) {
      retry_gap_s = 5;
      last_retry = 0;
    }
    had_link = link;

    if (millis() - last_retry > retry_gap_s * 1000UL) {
      last_retry = millis();
      if (connectAndAnnounce()) {
        retry_gap_s = 15;
        flushBacklog();
      } else if (retry_gap_s < 240) {
        retry_gap_s *= 2;
      }
    }
  }

  if (rtc_mode == MODE_DRIVING || rtc_mode == MODE_MOVED) {
    if (!gnss::enabled()) gnss::enable();

#if MODEM_HAS_GNSS
    double mlat, mlon;
    float mspd, mcrs, malt, mhdop;
    int msat;
    uint32_t mts;
    if (transport::modemGnssFix(mlat, mlon, mspd, mcrs, malt, msat, mhdop, mts)) {
      gnss::feedModemFix(mlat, mlon, mspd, mcrs, malt, msat, mhdop, mts);
    }
#endif

    PosRecord rec = {};
    if (gnss::fill(rec, cfg.hdop_max)) {
      const bool due = (millis() - last_pos_ms) >= posIntervalMs();
      if (due || courseChangedEnough(rec)) {
        rec.seq = ++rtc_seq;
        rec.mode = rtc_mode;
        if (rec.ts == 0) {
          const uint32_t nt = transport::networkTime();
          if (nt) { rec.ts = nt; rec.ts_src = TS_NTP; }
        }
        sendOrQueue(rec);
        last_pos = rec;
        have_last_pos = true;
        rtc_last_course = rec.crs_deg;
        last_pos_ms = millis();
        saveSeq();
      }
    }

    if (millis() - last_tel_ms >= 60000UL) {
      last_tel_ms = millis();
      publishTelemetry();
      flushBacklog();
    }
    delay(50);
    return;
  }

  // PARKED or HIBERNATE: send what is due, then sleep. Staying awake here is
  // what would blow the budget, so there is no "just for a moment" path.
  //
  // The interval check is not redundant with the sleep below. On the bench and
  // on a power bank the device does not sleep at all (no voltage sense), and
  // without this guard the loop would republish telemetry and retry the broker
  // every few milliseconds. That is exactly what happened the first time: the
  // console filled with TLS retry errors at loop speed.
  const uint32_t park_interval_ms =
      (rtc_mode == MODE_HIBERNATE ? 6UL * 3600UL : cfg.int_park) * 1000UL;
  if (last_tel_ms == 0 || millis() - last_tel_ms >= park_interval_ms) {
    last_tel_ms = millis();
    publishTelemetry();
    flushBacklog();
  }
  parkedSleep();
}
