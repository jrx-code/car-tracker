// WiFi transport. Bench build only (phase 0 of docs/11-plan-wdrozenia.md) and
// the OTA path in the garage. Same interface as the LTE transport, so the
// application code is identical in both.
#if defined(TRANSPORT_WIFI)

#include <ESP_SSLClient.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <time.h>

#include "config.h"
#include "modem/broker_socket.h"
#include "modem/transport.h"
#include "settings/settings.h"

#if MODEM_HAS_GNSS
#include "modem/cgnssinfo.h"
#include "pins.h"
#include "power/power.h"
#endif

namespace transport {
namespace {

// TLS on top of a plain client. The same wrapper is used over the modem, so
// certificate handling does not change with the transport (docs/09 section 9.3).
WiFiClient tcp;
ESP_SSLClient net;
PubSubClient mqtt(net);
LinkInfo link;
MessageHandler handler;

void raw(char* topic, uint8_t* payload, unsigned int len) {
  if (handler) handler(topic, payload, len);
}

#if MODEM_HAS_GNSS
// Board with a modem but data over WiFi (env lilygo_wifi): the modem is used
// only as the GNSS receiver, over plain AT. No TinyGSM, no network attach.
HardwareSerial at(2);
enum class ModemState { Off, Up, Failed };
ModemState modem_state = ModemState::Off;
uint32_t last_query_ms = 0;

// Sends a command and collects the reply until OK/ERROR or the timeout.
String atCmd(const char* cmd, uint32_t timeout_ms) {
  while (at.available()) at.read();
  at.print(cmd);
  at.print("\r\n");
  String reply;
  const uint32_t t0 = millis();
  while (millis() - t0 < timeout_ms) {
    while (at.available()) reply += static_cast<char>(at.read());
    if (reply.indexOf("OK\r") >= 0 || reply.indexOf("ERROR") >= 0) break;
    delay(5);
  }
  return reply;
}

bool modemUp() {
  if (modem_state == ModemState::Up) return true;
  if (modem_state == ModemState::Failed) return false;
  power::modemPower(true);
  delay(200);
  at.begin(115200, SERIAL_8N1, PIN_MODEM_RX, PIN_MODEM_TX);
  // Same PWRKEY pulse as the LTE transport; the A7670 needs >= 50 ms low at
  // the module pin, the board inverts it through a transistor.
  pinMode(PIN_MODEM_PWRKEY, OUTPUT);
  digitalWrite(PIN_MODEM_PWRKEY, LOW);
  delay(100);
  digitalWrite(PIN_MODEM_PWRKEY, HIGH);
  delay(1200);
  digitalWrite(PIN_MODEM_PWRKEY, LOW);

  const uint32_t t0 = millis();
  while (millis() - t0 < 15000) {
    if (atCmd("AT", 1000).indexOf("OK") >= 0) {
      atCmd("ATE0", 500);
      // The receiver reports "+CGNSSPWR: READY!" a few seconds later; until
      // then CGNSSINFO returns the empty line, which the parser rejects.
      atCmd("AT+CGNSSPWR=1", 2000);
      Serial.printf("gnss: modem up after %lu ms\n", millis() - t0);
      modem_state = ModemState::Up;
      return true;
    }
  }
  // Do not retry on every loop pass: each attempt blocks for 15 s.
  Serial.println("gnss: modem does not answer AT");
  power::modemPower(false);
  modem_state = ModemState::Failed;
  return false;
}
#endif

// A stored PEM that parses to zero certificates leaves TLS with no trust
// anchor, and the only symptom is "chain could not be linked" much later.
// Seen 2026-09-28 after an upload that turned every '+' into a space.
void warnIfNoAnchor(const String& pem) {
  X509List probe(pem.c_str());
  if (probe.getCount() == 0) {
    Serial.println("wifi: stored CA has no parsable certificate, TLS will fail");
  }
}

}  // namespace

bool begin() {
  const settings::Settings& cfg = settings::get();
  strncpy(link.net, "WIFI", sizeof(link.net) - 1);
  // The portal owns the WiFi join; this transport only opens the socket.
  // SSL off here on purpose: TLS is started in openBrokerSocket(), see there.
  net.setClient(&tcp, false);
#if defined(ENABLE_DEBUG)
  net.setDebugLevel(4);  // TLS diagnostics: build with -DENABLE_DEBUG
#endif

  // Certificate from the portal if one was uploaded, otherwise the compiled-in
  // default. Verification can be turned off deliberately for a bench broker.
  static String ca;
  ca = settings::caCert();
  if (!cfg.mqtt_verify_ca) {
    net.setInsecure();
  } else if (ca.length() > 100) {
    net.setCACert(ca.c_str());
    warnIfNoAnchor(ca);
  } else {
    net.setCACert(MQTT_ROOT_CA);
  }
  // BearSSL buffers. 4 kB receive is enough for a normal certificate chain;
  // smaller only works when the broker honours max_fragment_length.
  net.setBufferSizes(4096, 1024);
  mqtt.setServer(cfg.mqtt_host, cfg.mqtt_port);
  mqtt.setKeepAlive(cfg.mqtt_keepalive);
  mqtt.setBufferSize(MQTT_MAX_PACKET_SIZE);
  mqtt.setCallback(raw);
  strncpy(link.imei, WiFi.macAddress().c_str(), sizeof(link.imei) - 1);
  return true;
}

bool connect(const char* client_id, const char* user, const char* pass,
             const char* lwt_topic, const char* lwt_payload) {
  // The portal keeps the station associated and falls back to its own AP; this
  // transport must not fight it for the radio, it only waits for a link.
  if (WiFi.status() != WL_CONNECTED) return false;
  static bool time_set = false;
  if (!time_set) {
    time_set = true;
    configTime(0, 0, "pool.ntp.org");
  }
  link.rssi = WiFi.RSSI();

  if (mqtt.connected()) return true;
  // Certificate dates are checked against time(). Before the first NTP answer
  // that is seconds since boot, i.e. 1970, and BearSSL rejects the broker as
  // "not yet valid" (seen on the bench 2026-09-28). Wait for the clock instead.
  if (settings::get().mqtt_verify_ca && networkTime() == 0) {
    Serial.println("wifi: waiting for NTP before TLS");
    return false;
  }
  // LWT is what turns an unplugged tracker into an unavailable entity in HA
  // within the keepalive window, instead of a frozen last position
  // (acceptance criterion 6 in docs/01).
  if (!openBrokerSocket(net, tcp)) return false;
  return mqtt.connect(client_id, user, pass, lwt_topic, 1, true, lwt_payload);
}

bool connected() { return WiFi.status() == WL_CONNECTED && mqtt.connected(); }
void loop() { mqtt.loop(); }

bool publish(const char* topic, const char* payload, bool retain) {
  return mqtt.publish(topic, payload, retain);
}

bool subscribe(const char* topic) { return mqtt.subscribe(topic, 1); }
void onMessage(MessageHandler h) { handler = h; }
// The portal owns the WiFi link and runs from loop(); nothing here blocks long
// enough on an AT exchange to need it.
void setIdleHook(IdleHook) {}

void sleep() {
#if MODEM_HAS_GNSS
  if (modem_state == ModemState::Up) power::modemPower(false);
  modem_state = ModemState::Off;
#endif
  mqtt.disconnect();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

const LinkInfo& info() {
  link.rssi = WiFi.RSSI();
  return link;
}

uint32_t networkTime() {
  time_t now = time(nullptr);
  return (now > 1700000000) ? static_cast<uint32_t>(now) : 0;
}

#if MODEM_HAS_GNSS
bool modemGnssFix(double& lat, double& lon, float& speed_kmh, float& course,
                  float& alt, int& sats, float& hdop, uint32_t& utc_ts) {
  // The receiver updates once per second; asking faster only adds UART load.
  if (millis() - last_query_ms < 1000) return false;
  last_query_ms = millis();
  if (!modemUp()) return false;

  const String reply = atCmd("AT+CGNSSINFO", 1000);
  const int at_line = reply.indexOf("+CGNSSINFO:");
  if (at_line < 0) return false;
  cgnss::Fix fix;
  if (!cgnss::parse(reply.c_str() + at_line + strlen("+CGNSSINFO:"), fix)) return false;
  lat = fix.lat;
  lon = fix.lon;
  speed_kmh = fix.speed_kmh;
  course = fix.course;
  alt = fix.alt;
  sats = fix.sats;
  hdop = fix.hdop;
  utc_ts = fix.utc_ts;
  return true;
}
#else
bool modemGnssFix(double&, double&, float&, float&, float&, int&, float&,
                  uint32_t&) {
  return false;  // no modem in this build
}
#endif

}  // namespace transport

#endif  // TRANSPORT_WIFI
