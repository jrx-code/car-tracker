// LTE transport over TinyGSM. One file for every SIMCom variant; the
// differences are in platformio.ini build flags, not here (docs/03).
#if defined(TRANSPORT_LTE)

#include <ESP_SSLClient.h>
#include <PubSubClient.h>
#include <TinyGsmClient.h>

#include "config.h"
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

bool powerUp() {
  if (powered) return true;
  // The rail is switched, not just the chip, so this is a cold start every time
  // we come back from PARKED. That is the deliberate trade in docs/04.
  power::modemPower(true);
  delay(200);
  atSerial.begin(MODEM_BAUD, SERIAL_8N1, PIN_MODEM_RX, PIN_MODEM_TX);
  pulsePwrKey();

  const uint32_t t0 = millis();
  while (millis() - t0 < 15000) {
    if (modem.testAT(1000)) {
      powered = true;
      return true;
    }
  }
  power::modemPower(false);
  return false;
}

int16_t csqToDbm(int16_t csq) {
  if (csq < 0 || csq == 99) return 0;
  return static_cast<int16_t>(-113 + 2 * csq);
}

}  // namespace

bool begin() {
  const settings::Settings& cfg = settings::get();
  netClient.setClient(&tcp, true);
  static String ca;
  ca = settings::caCert();
  if (!cfg.mqtt_verify_ca) {
    netClient.setInsecure();
  } else if (ca.length() > 100) {
    netClient.setCACert(ca.c_str());
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
  if (!powerUp()) return false;

  const settings::Settings& cfg = settings::get();
  if (strlen(cfg.sim_pin) > 0 && modem.getSimStatus() != 3) {
    // A locked SIM after three wrong attempts means a trip to the car, so the
    // PIN is normally disabled on the card instead (docs/09 section 9.2).
    modem.simUnlock(cfg.sim_pin);
  }

  if (!modem.waitForNetwork(60000L, true)) return false;
  if (!modem.gprsConnect(cfg.apn, cfg.apn_user, cfg.apn_pass)) return false;
  if (!modem.isGprsConnected()) return false;

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
  return mqtt.connect(client_id, user, pass, lwt_topic, 1, true, lwt_payload);
}

bool connected() { return powered && modem.isGprsConnected() && mqtt.connected(); }
void loop() { mqtt.loop(); }

bool publish(const char* topic, const char* payload, bool retain) {
  return mqtt.publish(topic, payload, retain);
}

bool subscribe(const char* topic) { return mqtt.subscribe(topic, 1); }
void onMessage(MessageHandler h) { handler = h; }

void sleep() {
  if (mqtt.connected()) mqtt.disconnect();
  if (powered) {
    modem.gprsDisconnect();
    modem.poweroff();
  }
  power::modemPower(false);
  atSerial.end();
  powered = false;
}

const LinkInfo& info() {
  if (powered) link.rssi = csqToDbm(modem.getSignalQuality());
  return link;
}

uint32_t networkTime() {
  if (!powered) return 0;
  int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
  float tz = 0;
  if (!modem.getNetworkTime(&year, &month, &day, &hour, &minute, &second, &tz)) {
    return 0;
  }
  // Network time carries a timezone offset; normalise it to UTC.
  const uint32_t local = timeutil::toUnixUtc(year, month, day, hour, minute, second);
  if (local == 0) return 0;
  return static_cast<uint32_t>(local - static_cast<int32_t>(tz * 3600.0f));
}

bool modemGnssFix(double& lat, double& lon, float& speed_kmh, float& course,
                  float& alt, int& sats, float& hdop, uint32_t& utc_ts) {
#if MODEM_HAS_GNSS
  if (!powered) return false;
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

#endif  // TRANSPORT_LTE
