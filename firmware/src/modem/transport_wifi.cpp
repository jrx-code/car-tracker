// WiFi transport. Bench build only (phase 0 of docs/11-plan-wdrozenia.md) and
// the OTA path in the garage. Same interface as the LTE transport, so the
// application code is identical in both.
#if defined(TRANSPORT_WIFI)

#include <ESP_SSLClient.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <time.h>

#include "config.h"
#include "modem/transport.h"
#include "settings/settings.h"

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

}  // namespace

bool begin() {
  const settings::Settings& cfg = settings::get();
  strncpy(link.net, "WIFI", sizeof(link.net) - 1);
  // The portal owns the WiFi join; this transport only opens the socket.
  net.setClient(&tcp, true);

  // Certificate from the portal if one was uploaded, otherwise the compiled-in
  // default. Verification can be turned off deliberately for a bench broker.
  static String ca;
  ca = settings::caCert();
  if (!cfg.mqtt_verify_ca) {
    net.setInsecure();
  } else if (ca.length() > 100) {
    net.setCACert(ca.c_str());
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
  // LWT is what turns an unplugged tracker into an unavailable entity in HA
  // within the keepalive window, instead of a frozen last position
  // (acceptance criterion 6 in docs/01).
  return mqtt.connect(client_id, user, pass, lwt_topic, 1, true, lwt_payload);
}

bool connected() { return WiFi.status() == WL_CONNECTED && mqtt.connected(); }
void loop() { mqtt.loop(); }

bool publish(const char* topic, const char* payload, bool retain) {
  return mqtt.publish(topic, payload, retain);
}

bool subscribe(const char* topic) { return mqtt.subscribe(topic, 1); }
void onMessage(MessageHandler h) { handler = h; }

void sleep() {
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

bool modemGnssFix(double&, double&, float&, float&, float&, int&, float&,
                  uint32_t&) {
  return false;  // no modem in this build
}

}  // namespace transport

#endif  // TRANSPORT_WIFI
