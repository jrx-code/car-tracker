// Opening the broker socket, shared by the WiFi and the LTE transport.
#pragma once

#include <Client.h>
#include <ESP_SSLClient.h>

#include "settings/settings.h"
#include "util/hostcheck.h"

namespace transport {

// ESP_SSLClient decides on TLS by port number: connect() runs the handshake
// only for ports on its built-in list (443, 8883, ...) and sends plain text to
// any other port, without an error. On 2026-10-08 that put an MQTT CONNECT with
// the password in clear on the internet, towards a broker listening on 8885.
//
// So the TLS client is set up with SSL disabled (setClient(..., false)), which
// makes connect() plain TCP for every port, and the handshake is started here
// explicitly whenever mqtt_tls is on. If it fails, the socket is closed before
// PubSubClient can write a byte. PubSubClient::connect() skips its own connect
// when the client already reports connected().
// Plain text, or TLS that does not check the certificate, hands the MQTT
// password to anyone on the path. Both stay possible for a bench broker on the
// LAN, never for a public name; the portal can still set them, the connect
// refuses. Checked before every connect, so a later cfg change cannot slip by.
inline bool brokerSecurityAllowed() {
  const settings::Settings& cfg = settings::get();
  if ((cfg.mqtt_tls && cfg.mqtt_verify_ca) || isLocalBrokerHost(cfg.mqtt_host)) return true;
  Serial.printf("mqtt: refusing %s to public host %s\n",
                cfg.mqtt_tls ? "TLS without certificate check" : "plain text",
                cfg.mqtt_host);
  return false;
}

inline bool openBrokerSocket(ESP_SSLClient& tls, Client& basic) {
  const settings::Settings& cfg = settings::get();
  if (!brokerSecurityAllowed()) return false;
  if (tls.connected()) {
    if (!cfg.mqtt_tls || tls.isSecure()) return true;
    basic.stop();  // a plain socket left over while TLS is required
  }
  if (!tls.connect(cfg.mqtt_host, cfg.mqtt_port)) return false;
  if (!cfg.mqtt_tls) return true;
  if (!tls.connectSSL() || !tls.isSecure()) {
    char why[96] = "";
    const int err = tls.getLastSSLError(why, sizeof(why));
    // ESP_SSLClient::stop() returns early on a socket that never became
    // secure, so the TCP connection is closed on the basic client itself.
    tls.stop();
    basic.stop();
    Serial.printf("mqtt: TLS handshake failed, nothing sent (BearSSL %d: %s)\n", err, why);
    return false;
  }
  return true;
}

}  // namespace transport
