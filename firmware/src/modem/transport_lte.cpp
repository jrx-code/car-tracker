// LTE transport over TinyGSM. One file for every SIMCom variant; the
// differences are in platformio.ini build flags, not here (docs/03).
#if defined(TRANSPORT_LTE)

// TinyGSM spins on TINY_GSM_YIELD() while it waits for a modem response, and
// some of those waits are 60 s (CGACT, CGATT, NETCLOSE). Routing the yield to
// the idle hook keeps the portal alive through them. This is the only file that
// includes TinyGSM, so the macro is the same for every instantiation.
void transportLteIdle();
#define TINY_GSM_YIELD() transportLteIdle()

#include <ESP_SSLClient.h>
#include <PubSubClient.h>
#include <TinyGsmClient.h>

#include "config.h"
#include "modem/cgnssinfo.h"
#include "modem/broker_socket.h"
#include "modem/transport.h"
#include "pins.h"
#include "power/power.h"
#include "settings/settings.h"
#include "util/timeutil.h"

namespace transport {
namespace {

HardwareSerial atSerial(2);
TinyGsm modem(atSerial);

// TLS is terminated on the ESP32, not inside the modem. That costs some RAM,
// but it keeps one certificate path for every modem variant and avoids having
// to load the CA into each module with vendor specific AT commands.
TinyGsmClient tcp(modem);
ESP_SSLClient netClient;

PubSubClient mqtt(netClient);
LinkInfo link;
MessageHandler handler;
bool powered = false;
// The modem rail is cut in PARKED, so the receiver has to be switched on again
// after every cold start, not once per boot of the ESP32.
bool gnss_started = false;

IdleHook idle_hook = nullptr;
// The hook only runs inside the attach phases (power up, registration, PDP).
// The TLS handshake is left out: it runs deep inside BearSSL, and serving a
// portal request on top of that stack has not been tested.
bool idle_allowed = false;
// True while the hook runs, i.e. while an AT command is waiting for its answer.
// Any AT sent now would interleave with it, so public calls answer from cache.
bool in_hook = false;
bool last_connected = false;
// Set once the PDP context is up, cleared on sleep and on a failed attach.
// Without it every loop pass asked a modem with no data link (or not answering
// at all) for CGATT and ran the MQTT client over a dead socket, each call
// sitting in its full AT timeout: 1 s for CGATT?, 3-5 s for mqtt.loop, up to
// 8 s for one telemetry packet. Measured 2026-10-01, portal stalled as long.
bool link_up = false;

// Opens a window in which waits call the idle hook; closes it on scope exit.
struct IdleWindow {
  IdleWindow() { idle_allowed = true; }
  ~IdleWindow() { idle_allowed = false; }
};

// Network time goes into every telemetry packet and event. Each AT+CCLK? to a
// modem that is not answering costs its full timeout (2 s per packet, measured
// 2026-10-01), so the last good reading is kept and extrapolated from millis(),
// and the modem is asked again at most once a minute.
uint32_t nt_base = 0;
uint32_t nt_at_ms = 0;
uint32_t nt_tried_ms = 0;

// The first attach after boot failed with CREG 0 and CSQ 99 for the whole
// wait while raw AT registered in 15-29 s (2026-10-08). Dump what decides
// whether the radio even searches, so a failure in the field explains itself.
void logModemState() {
  for (const char* q : {"+CFUN?", "+CPIN?", "+CEREG?", "+COPS?", "+CPSI?", "+CGNSSPWR?"}) {
    String r;
    modem.sendAT(q);
    modem.waitResponse(2000L, r);
    r.replace("\r\n", " ");
    r.trim();
    Serial.printf("lte: AT%s -> %s\n", q, r.c_str());
  }
}

// Sets the modem clock over NTP, for networks that send no NITZ (or not yet).
// Needs the data link; the module answers +CNTP: 0 once the clock is set.
bool syncClockNtp() {
  modem.sendAT(GF("+CNTP=\"pool.ntp.org\",0"));
  if (modem.waitResponse(2000L) != 1) return false;
  modem.sendAT(GF("+CNTP"));
  if (modem.waitResponse(2000L) != 1) return false;
  const bool ok = modem.waitResponse(15000L, GF("+CNTP: 0")) == 1;
  Serial.printf("lte: NTP %s\n", ok ? "set the clock" : "failed");
  return ok;
}

// Asks the modem every time; networkTime() is the cached front for it.
uint32_t queryNetworkTime() {
  nt_tried_ms = millis();
  int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
  float tz = 0;
  if (!modem.getNetworkTime(&year, &month, &day, &hour, &minute, &second, &tz)) {
    return 0;
  }
  // Network time carries a timezone offset; normalise it to UTC.
  const uint32_t local = timeutil::toUnixUtc(year, month, day, hour, minute, second);
  if (local == 0) return 0;
  nt_base = static_cast<uint32_t>(local - static_cast<int32_t>(tz * 3600.0f));
  nt_at_ms = millis();
  return nt_base;
}

void raw(char* topic, uint8_t* payload, unsigned int len) {
  if (handler) handler(topic, payload, len);
}

// PWRKEY pulse. Length differs between families; these are the values from the
// respective hardware design guides.
void pulsePwrKey() {
  if (PIN_MODEM_PWRKEY < 0) return;
  pinMode(PIN_MODEM_PWRKEY, OUTPUT);
  digitalWrite(PIN_MODEM_PWRKEY, LOW);
  delay(100);
  digitalWrite(PIN_MODEM_PWRKEY, HIGH);
#if defined(MODEM_PROFILE_SIM7080G)
  delay(1000);
#else
  delay(1200);
#endif
  digitalWrite(PIN_MODEM_PWRKEY, LOW);
}

// Cuts the rail without talking to the modem first: for a module that has
// stopped answering AT, where sleep() would sit in NETCLOSE and CGATT=0 for a
// minute each. Seen 2026-10-08: after a hang the modem stayed silent, the
// firmware still counted it as powered and never brought it back, and in a
// car nobody pulls the plug.
void powerCycle() {
  Serial.println("lte: modem does not answer AT, power-cycling it");
  link_up = false;
  last_connected = false;
  nt_tried_ms = 0;
  power::modemPower(false);
  atSerial.end();
  powered = false;
  delay(3000);  // let the 3.8 V rail fall; a short dip leaves the module up
}

bool powerUp() {
  if (powered) {
    if (modem.testAT(2000)) return true;
    powerCycle();
  }
  // The rail is switched, not just the chip, so this is a cold start every time
  // we come back from PARKED. That is the deliberate trade in docs/04.
  // RESET is active HIGH on the LilyGO and nothing drove it, so the line was
  // left to the ESP32 strapping pull-up on GPIO 5. Hold it released.
  if (PIN_MODEM_RESET >= 0) {
    pinMode(PIN_MODEM_RESET, OUTPUT);
    digitalWrite(PIN_MODEM_RESET, LOW);
  }
  power::modemPower(true);
  delay(200);
  atSerial.begin(MODEM_BAUD, SERIAL_8N1, PIN_MODEM_RX, PIN_MODEM_TX);
  pulsePwrKey();

  const uint32_t t0 = millis();
  while (millis() - t0 < 15000) {
    if (modem.testAT(1000)) {
      powered = true;
      gnss_started = false;
      // Network time (NITZ) updates the modem clock only with CTZU on, and it
      // shipped off: the clock stayed at its 70/01/01 default (2026-10-08).
      // The setting is kept across restarts; sending it again costs one AT.
      modem.sendAT(GF("+CTZU=1"));
      modem.waitResponse(1000L);
      Serial.printf("lte: modem up after %lu ms\n", millis() - t0);
      return true;
    }
  }
  Serial.println("lte: modem does not answer AT");
  power::modemPower(false);
  return false;
}

int16_t csqToDbm(int16_t csq) {
  if (csq < 0 || csq == 99) return 0;
  return static_cast<int16_t>(-113 + 2 * csq);
}

// A stored PEM that parses to zero certificates leaves TLS with no trust
// anchor, and the only symptom is "chain could not be linked" much later.
// Seen 2026-09-28 after an upload that turned every '+' into a space.
void warnIfNoAnchor(const String& pem) {
  X509List probe(pem.c_str());
  if (probe.getCount() == 0) {
    Serial.println("lte: stored CA has no parsable certificate, TLS will fail");
  }
}

}  // namespace

bool begin() {
  const settings::Settings& cfg = settings::get();
  // SSL off here on purpose: TLS is started in openBrokerSocket(), see there.
  netClient.setClient(&tcp, false);
#if defined(ENABLE_DEBUG)
  netClient.setDebugLevel(4);  // TLS diagnostics: build with -DENABLE_DEBUG
#endif
  static String ca;
  ca = settings::caCert();
  if (!cfg.mqtt_verify_ca) {
    netClient.setInsecure();
  } else if (ca.length() > 100) {
    netClient.setCACert(ca.c_str());
    warnIfNoAnchor(ca);
  } else {
    netClient.setCACert(MQTT_ROOT_CA);
  }
  netClient.setBufferSizes(4096, 1024);
  mqtt.setServer(cfg.mqtt_host, cfg.mqtt_port);
  mqtt.setKeepAlive(cfg.mqtt_keepalive);
  mqtt.setBufferSize(MQTT_MAX_PACKET_SIZE);
  mqtt.setCallback(raw);
  return true;
}

bool connect(const char* client_id, const char* user, const char* pass,
             const char* lwt_topic, const char* lwt_payload) {
  const settings::Settings& cfg = settings::get();
  {
    // Up to 15 s for AT, 60 s for registration and up to 60 s per command for
    // the PDP context; a SIM without data sits in those waits (2026-09-28).
    // The portal has to keep running through them.
    IdleWindow window;
    link_up = false;
    if (!powerUp()) return false;

    if (strlen(cfg.sim_pin) > 0 && modem.getSimStatus() != 3) {
      // A locked SIM after three wrong attempts means a trip to the car, so the
      // PIN is normally disabled on the card instead (docs/09 section 9.2).
      modem.simUnlock(cfg.sim_pin);
    }

    // Registration after a cold start took 66 s on Orange (2026-10-08).
    if (!modem.waitForNetwork(120000L, true)) {
      Serial.printf("lte: no network, reg=%d csq=%d\n",
                    static_cast<int>(modem.getRegistrationStatus()),
                    static_cast<int>(modem.getSignalQuality()));
      logModemState();
      return false;
    }
    if (!modem.gprsConnect(cfg.apn, cfg.apn_user, cfg.apn_pass) ||
        !modem.isGprsConnected()) {
      Serial.printf("lte: data attach failed, apn=%s\n", cfg.apn);
      return false;
    }
    link_up = true;
  }
  Serial.printf("lte: online, %s\n", modem.getLocalIP().c_str());

  link.rssi = csqToDbm(modem.getSignalQuality());
  link.roaming = modem.isNetworkConnected() && modem.getRegistrationStatus() == 5;
  modem.getOperator().toCharArray(link.oper, sizeof(link.oper));
  modem.getIMEI().toCharArray(link.imei, sizeof(link.imei));
  modem.getSimCCID().toCharArray(link.iccid, sizeof(link.iccid));

#if defined(MODEM_PROFILE_SIM7080G)
  strncpy(link.net, "LTE-M", sizeof(link.net) - 1);
#else
  strncpy(link.net, "LTE", sizeof(link.net) - 1);
#endif

  if (mqtt.connected()) return true;
  // Nothing sets the ESP32 clock on this path, and BearSSL checks certificate
  // dates against it; hand it the network time instead (see transport_wifi).
  if (cfg.mqtt_verify_ca) {
    // Asked directly: a stale or missing cached time must not fail the handshake.
    uint32_t nt = queryNetworkTime();
    if (nt == 0 && syncClockNtp()) nt = queryNetworkTime();
    if (nt == 0) {
      Serial.println("lte: no network time yet, TLS postponed");
      return false;
    }
    netClient.setX509Time(nt);
    Serial.printf("lte: certificate check against network time %lu\n",
                  static_cast<unsigned long>(nt));
  }
  if (!openBrokerSocket(netClient, tcp)) return false;
  const bool ok = mqtt.connect(client_id, user, pass, lwt_topic, 1, true, lwt_payload);
  Serial.printf("lte: mqtt %s, state=%d\n", ok ? "connected" : "failed", mqtt.state());
  return ok;
}

bool connected() {
  if (in_hook) return last_connected;
  last_connected = powered && link_up && modem.isGprsConnected() && mqtt.connected();
  return last_connected;
}

void loop() {
  if (link_up && !in_hook) mqtt.loop();
}

bool publish(const char* topic, const char* payload, bool retain) {
  if (!link_up || in_hook) return false;
  return mqtt.publish(topic, payload, retain);
}

bool subscribe(const char* topic) {
  if (!link_up || in_hook) return false;
  return mqtt.subscribe(topic, 1);
}

void onMessage(MessageHandler h) { handler = h; }
void setIdleHook(IdleHook hook) { idle_hook = hook; }

void sleep() {
  if (link_up && mqtt.connected()) mqtt.disconnect();
  last_connected = false;
  link_up = false;
  nt_tried_ms = 0;
  if (powered) {
    // NETCLOSE and CGATT=0 wait up to 60 s each.
    IdleWindow window;
    modem.gprsDisconnect();
    modem.poweroff();
  }
  power::modemPower(false);
  atSerial.end();
  powered = false;
}

const LinkInfo& info() {
  if (powered && link_up && !in_hook) link.rssi = csqToDbm(modem.getSignalQuality());
  return link;
}

uint32_t networkTime() {
  if (!powered) return 0;
  const uint32_t now = millis();
  const bool asked_recently = nt_tried_ms != 0 && now - nt_tried_ms < 60000UL;
  if (!in_hook && !asked_recently) queryNetworkTime();
  return nt_base ? nt_base + (millis() - nt_at_ms) / 1000UL : 0;
}

namespace {
char last_gnss_line[100] = "";
void rememberGnssLine(const char* line) {
  while (*line == ' ') line++;
  strncpy(last_gnss_line, line, sizeof(last_gnss_line) - 1);
  last_gnss_line[sizeof(last_gnss_line) - 1] = '\0';
  last_gnss_line[strcspn(last_gnss_line, "\r\n")] = '\0';
}
}  // namespace

const char* lastGnssLine() { return last_gnss_line; }

bool modemGnssFix(double& lat, double& lon, float& speed_kmh, float& course,
                  float& alt, int& sats, float& hdop, uint32_t& utc_ts) {
#if MODEM_HAS_GNSS && defined(TINY_GSM_MODEM_A7672X)
  if (!powered || in_hook) return false;
  // TinyGSM 0.12 has no GNSS API for the A7672X class, so this talks AT
  // directly and parses the line in modem/cgnssinfo.h.
  if (!gnss_started) {
    modem.sendAT(GF("+CGNSSPWR=1"));
    if (modem.waitResponse(2000L) != 1) return false;
    // "+CGNSSPWR: READY!" arrives as a URC a few seconds later; until then
    // CGNSSINFO just returns the empty no-fix line, which parse() rejects.
    gnss_started = true;
  }
  modem.sendAT(GF("+CGNSSINFO"));
  if (modem.waitResponse(2000L, GF("+CGNSSINFO:")) != 1) return false;
  const String line = atSerial.readStringUntil('\n');
  rememberGnssLine(line.c_str());
  modem.waitResponse();

  cgnss::Fix fix;
  if (!cgnss::parse(line.c_str(), fix)) {
    // The empty no-fix line is ",,,,,,,,..."; anything carrying digits that
    // the parser still rejects is a format question (docs/07), so log it.
    if (strpbrk(line.c_str(), "123456789")) {
      Serial.printf("gnss: CGNSSINFO not parsed: %s\n", line.c_str());
    }
    return false;
  }
  // The format with a real fix was never seen before the field test; keep the
  // first good line of every boot in the log to check the parser against it.
  static bool logged_first_fix = false;
  if (!logged_first_fix) {
    logged_first_fix = true;
    Serial.printf("gnss: first fix line: %s\n", line.c_str());
  }
  lat = fix.lat;
  lon = fix.lon;
  speed_kmh = fix.speed_kmh;
  course = fix.course;
  alt = fix.alt;
  sats = fix.sats;
  hdop = fix.hdop;
  utc_ts = fix.utc_ts;
  return true;
#elif MODEM_HAS_GNSS
  if (!powered || in_hook) return false;
  static bool gps_started = false;
  if (!gps_started) {
    modem.enableGPS();
    gps_started = true;
  }
  // TinyGSM hands coordinates back as float, which is about half a metre of
  // resolution at our latitude. Good enough for a car, but it is the reason the
  // record keeps degrees as int32 * 1e7 rather than passing floats around.
  float flat = 0, flon = 0, accuracy = 0;
  int vsat = 0, usat = 0;
  int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
  if (!modem.getGPS(&flat, &flon, &speed_kmh, &alt, &vsat, &usat, &accuracy,
                    &year, &month, &day, &hour, &minute, &second)) {
    return false;
  }
  lat = flat;
  lon = flon;
  sats = usat;
  hdop = accuracy;  // the modem reports accuracy, not HDOP; treated as equivalent
  course = 0;       // not provided by this call, GNSS course comes from the NEO-6M
  utc_ts = timeutil::toUnixUtc(year, month, day, hour, minute, second);
  return true;
#else
  (void)lat; (void)lon; (void)speed_kmh; (void)course; (void)alt;
  (void)sats; (void)hdop; (void)utc_ts;
  return false;  // module without GNSS, e.g. an A7670 with the LASE suffix
#endif
}

}  // namespace transport

void transportLteIdle() {
  using namespace transport;
  if (!idle_allowed || in_hook || idle_hook == nullptr) {
    delay(0);  // TinyGSM's own default yield
    return;
  }
  in_hook = true;
  idle_hook();
  in_hook = false;
}

#endif  // TRANSPORT_LTE
