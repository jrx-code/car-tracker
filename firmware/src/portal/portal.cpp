#include "portal.h"

#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>

#include "portal/portal_page.h"
#include "settings/settings.h"

namespace portal {
namespace {

WebServer server(80);
DNSServer dns;
StatusProvider status_provider;

bool ap_up = false;
uint32_t offline_since_ms = 0;
uint32_t ap_started_ms = 0;
uint32_t last_sta_try_ms = 0;

constexpr uint16_t kDnsPort = 53;

// Every write goes through here. Reading status and settings stays open: the
// portal is reachable on the home network and, in the car, only over the
// device's own AP, and a lock that stops the owner reading the page while
// standing next to a dead tracker is worse than useless.
bool authorised() {
  return settings::adminPasswordMatches(server.header("X-Admin-Pass"));
}

void sendJson(int code, const String& json) {
  server.send(code, "application/json; charset=utf-8", json);
}

void sendError(int code, const String& message) {
  JsonDocument doc;
  doc["error"] = message;
  String out;
  serializeJson(doc, out);
  sendJson(code, out);
}

// Reading the request body out of WebServer is not as simple as arg("plain"):
// with a form content type the body is parsed into named arguments instead and
// "plain" comes back empty. An empty body used to mean "delete the stored
// certificate", so a request with the wrong content type silently wiped it.
// Deleting is now an explicit action and an empty body here is an error.
String requestBody() {
  String body = server.arg("plain");
  if (body.length()) return body;
  // Fall back to reassembling a form-parsed body: a PEM sent as
  // x-www-form-urlencoded arrives as one argument whose name is the payload.
  for (int i = 0; i < server.args(); i++) {
    const String name = server.argName(i);
    if (name == "plain") continue;
    body += name;
    if (server.arg(i).length()) body += "=" + server.arg(i);
  }
  return body;
}

void handleRoot() {
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html; charset=utf-8", PORTAL_HTML);
}

void handleStatus() {
  String json = status_provider ? status_provider() : String("{}");
  // Splice the network facts in, so the status provider does not have to know
  // anything about WiFi.
  JsonDocument doc;
  deserializeJson(doc, json);
  doc["sta"] = WiFi.status() == WL_CONNECTED;
  doc["ap"] = ap_up;
  doc["ap_ssid"] = settings::apSsid();
  doc["rssi"] = WiFi.RSSI();
  doc["ip"] = ipAddress();
  doc["vehicle_name"] = settings::get().vehicle_name;
  String out;
  serializeJson(doc, out);
  sendJson(200, out);
}

void handleGetSettings() { sendJson(200, settings::toJson()); }

void handlePostSettings() {
  if (!authorised()) {
    sendError(401, "wrong or missing admin password");
    return;
  }
  String error;
  if (!settings::applyJson(requestBody(), error)) {
    sendError(400, error);
    return;
  }
  sendJson(200, "{\"ok\":true}");
}

void handlePostCa() {
  if (!authorised()) {
    sendError(401, "wrong or missing admin password");
    return;
  }
  const String body = requestBody();
  if (body.length() == 0) {
    sendError(400,
              "empty body. To remove the stored certificate use "
              "/api/action with {\"action\":\"delete_ca\"}");
    return;
  }
  String error;
  if (!settings::setCaCert(body, error)) {
    sendError(400, error);
    return;
  }
  sendJson(200, "{\"ok\":true}");
}

void handleScan() {
  const int n = WiFi.scanNetworks();
  JsonDocument doc;
  JsonArray arr = doc["networks"].to<JsonArray>();
  for (int i = 0; i < n && i < 30; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = WiFi.SSID(i);
    o["rssi"] = WiFi.RSSI(i);
    o["open"] = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
  }
  WiFi.scanDelete();
  String out;
  serializeJson(doc, out);
  sendJson(200, out);
}

void handleAction() {
  if (!authorised()) {
    sendError(401, "wrong or missing admin password");
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, requestBody()) != DeserializationError::Ok) {
    sendError(400, "body is not valid JSON");
    return;
  }
  const String action = doc["action"] | "";

  if (action == "reboot") {
    sendJson(200, "{\"ok\":true}");
    delay(300);
    ESP.restart();
  } else if (action == "factory_reset") {
    settings::factoryReset();
    sendJson(200, "{\"ok\":true}");
    delay(300);
    ESP.restart();
  } else if (action == "start_ap") {
    startAp();
    sendJson(200, "{\"ok\":true}");
  } else if (action == "stop_ap") {
    stopAp();
    sendJson(200, "{\"ok\":true}");
  } else if (action == "delete_ca") {
    String error;
    settings::setCaCert("", error);
    sendJson(200, "{\"ok\":true}");
  } else {
    sendError(400, "unknown action");
  }
}

void joinWifi() {
  const settings::Settings& s = settings::get();
  if (!s.wifi_enabled || s.wifi_ssid[0] == '\0') return;
  WiFi.setHostname(s.hostname);
  WiFi.begin(s.wifi_ssid, s.wifi_pass);
  Serial.printf("portal: joining %s\n", s.wifi_ssid);
}

}  // namespace

void setStatusProvider(StatusProvider provider) { status_provider = provider; }

bool apActive() { return ap_up; }
bool staConnected() { return WiFi.status() == WL_CONNECTED; }

String ipAddress() {
  if (ap_up) return WiFi.softAPIP().toString();
  if (WiFi.status() == WL_CONNECTED) return WiFi.localIP().toString();
  return String("0.0.0.0");
}

void startAp() {
  if (ap_up) return;
  const settings::Settings& s = settings::get();
  const String ssid = settings::apSsid();
  WiFi.mode(WIFI_AP_STA);  // keep trying to rejoin while the AP is up
  const bool ok = (s.ap_pass[0] == '\0')
                      ? WiFi.softAP(ssid.c_str())
                      : WiFi.softAP(ssid.c_str(), s.ap_pass);
  if (!ok) {
    Serial.println("portal: softAP failed");
    return;
  }
  ap_up = true;
  ap_started_ms = millis();
  // Captive portal: any hostname resolves to the device, so a phone lands on
  // the page instead of a browser error.
  dns.start(kDnsPort, "*", WiFi.softAPIP());
  Serial.printf("portal: AP %s up at %s\n", ssid.c_str(),
                WiFi.softAPIP().toString().c_str());
}

void stopAp() {
  if (!ap_up) return;
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  ap_up = false;
  Serial.println("portal: AP down");
}

void begin() {
  const settings::Settings& s = settings::get();
  if (!s.portal_enabled) {
    Serial.println("portal: disabled in settings");
    return;
  }

  WiFi.mode(WIFI_STA);
  joinWifi();
  offline_since_ms = millis();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/settings", HTTP_GET, handleGetSettings);
  server.on("/api/settings", HTTP_POST, handlePostSettings);
  server.on("/api/ca", HTTP_POST, handlePostCa);
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/action", HTTP_POST, handleAction);
  server.onNotFound(handleRoot);  // captive portal catch-all

  // WebServer only keeps headers it was told to collect.
  const char* headers[] = {"X-Admin-Pass"};
  server.collectHeaders(headers, 1);

  server.begin();
  Serial.println("portal: HTTP on :80");
}

void loop() {
  if (!settings::get().portal_enabled) return;

  server.handleClient();
  if (ap_up) dns.processNextRequest();

  const settings::Settings& s = settings::get();
  const uint32_t now = millis();

  if (WiFi.status() == WL_CONNECTED) {
    offline_since_ms = 0;
    static bool announced = false;
    if (!announced) {
      announced = true;
      Serial.printf("portal: %s at http://%s/\n", s.wifi_ssid,
                    WiFi.localIP().toString().c_str());
      if (MDNS.begin(s.hostname)) MDNS.addService("http", "tcp", 80);
    }
    // Once the station is back, the emergency AP has done its job. Keeping it
    // up would leave an open door on a device that is otherwise reachable.
    if (ap_up && s.ap_timeout_s > 0 && now - ap_started_ms > 30000) stopAp();
    return;
  }

  if (offline_since_ms == 0) offline_since_ms = now;

  // Retry the station join periodically; the AP does not stop this because the
  // radio runs in AP_STA mode.
  if (now - last_sta_try_ms > 30000) {
    last_sta_try_ms = now;
    joinWifi();
  }

  if (!ap_up && s.ap_enabled &&
      (now - offline_since_ms) > static_cast<uint32_t>(s.ap_after_s) * 1000UL) {
    Serial.printf("portal: offline for %u s, raising the AP\n", s.ap_after_s);
    startAp();
  }

  if (ap_up && s.ap_timeout_s > 0 &&
      (now - ap_started_ms) > static_cast<uint32_t>(s.ap_timeout_s) * 1000UL) {
    // An AP left up for days in a parked car is a power draw and an attack
    // surface; it comes back the next time the device notices it is offline.
    Serial.println("portal: AP idle timeout");
    stopAp();
    offline_since_ms = now;
  }
}

}  // namespace portal
