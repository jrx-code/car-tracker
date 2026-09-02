// GPS bring-up probe, WiFi edition.
//
// Answers, in this order, the questions that actually go wrong when wiring a
// NEO-6M for the first time:
//   1. Is anything at all arriving on the RX pin?      (raw byte counter)
//   2. At which baud rate?                             (automatic scan)
//   3. Are the frames valid NMEA?                      (sentence counter)
//   4. Does the receiver see satellites?               (satellites in view)
//   5. Does it have a fix, and how good is it?         (fix, HDOP, position)
//
// A module wired with TX and RX swapped shows zero bytes. A module on the wrong
// baud rate shows bytes but no valid sentences. Those two look identical in a
// plain "no GPS data" message, which is why they are separated here.
//
// With WiFi up the whole status is served over HTTP, so the board can sit on a
// windowsill or in the car running off a power bank, with no laptop attached.
//
// Wiring (matches firmware/include/pins.h):
//   NEO-6M TX  -> ESP32 GPIO26
//   NEO-6M RX  -> ESP32 GPIO27
//   NEO-6M VCC -> ESP32 3V3 (5V only if 3V3 gives nothing)
//   NEO-6M GND -> ESP32 GND
#include <Arduino.h>
#include <ESPmDNS.h>
#include <TinyGPSPlus.h>
#include <WebServer.h>
#include <WiFi.h>

#include "secrets.h"

constexpr int PIN_GNSS_RX = 26;  // ESP32 receives here, module TX goes here
constexpr int PIN_GNSS_TX = 27;  // ESP32 transmits here, module RX goes here
constexpr int PIN_LED = 2;

// 9600 is the u-blox default; clones are sometimes shipped at 38400.
constexpr uint32_t BAUDS[] = {9600, 38400, 115200, 4800, 57600};
constexpr size_t BAUD_COUNT = sizeof(BAUDS) / sizeof(BAUDS[0]);
constexpr uint32_t SCAN_MS = 3000;
constexpr const char* MDNS_NAME = "gps-probe";

HardwareSerial gpsSerial(1);
TinyGPSPlus gps;
WebServer server(80);

uint32_t detected_baud = 0;
uint32_t bytes_total = 0;
uint32_t last_report = 0;
uint32_t first_fix_ms = 0;  // millis() of the first ever fix, 0 = never
uint32_t boot_ms = 0;
uint8_t max_satellites = 0;
float best_hdop = 99.0f;
bool echo_raw = false;  // off by default: raw NMEA drowns everything else
String scan_summary = "not run yet";
String wifi_state = "laczenie";

struct ScanResult {
  uint32_t bytes;
  uint32_t sentences;
};

ScanResult tryBaud(uint32_t baud) {
  // updateBaudRate, never begin/end per attempt: HardwareSerial::end() detaches
  // every pin and deletes the UART driver, and re-begin()ing UART1 in a loop
  // floods the USB console with repeated output. One begin() in setup(), rate
  // changes only, keeps the port stable.
  gpsSerial.updateBaudRate(baud);
  delay(150);
  while (gpsSerial.available()) gpsSerial.read();  // drop partial frames

  TinyGPSPlus probe;
  ScanResult r{0, 0};
  const uint32_t deadline = millis() + SCAN_MS;
  while (millis() < deadline) {
    while (gpsSerial.available()) {
      const char c = gpsSerial.read();
      r.bytes++;
      probe.encode(c);
    }
    server.handleClient();  // keep the page answering during the scan
    delay(1);
  }
  r.sentences = probe.passedChecksum();
  return r;
}

void scan() {
  Serial.println();
  Serial.println("=== GPS probe: scanning baud rates ===");
  Serial.printf("RX pin GPIO%d (module TX), TX pin GPIO%d (module RX)\n", PIN_GNSS_RX,
                PIN_GNSS_TX);

  uint32_t best_baud = 0;
  uint32_t best_sentences = 0;
  uint32_t any_bytes = 0;
  String summary;

  for (size_t i = 0; i < BAUD_COUNT; i++) {
    const ScanResult r = tryBaud(BAUDS[i]);
    any_bytes += r.bytes;
    Serial.printf("  %6lu baud: %5lu bytes, %3lu valid NMEA sentences\n", BAUDS[i],
                  r.bytes, r.sentences);
    summary += String(BAUDS[i]) + " baud: " + String(r.bytes) + " B, " +
               String(r.sentences) + " ramek NMEA\n";
    if (r.sentences > best_sentences) {
      best_sentences = r.sentences;
      best_baud = BAUDS[i];
    }
  }

  if (best_baud != 0) {
    detected_baud = best_baud;
    summary += "OK: odbiornik na " + String(detected_baud) + " baud";
    Serial.printf("\nOK: receiver talking at %lu baud.\n", detected_baud);
  } else if (any_bytes > 0) {
    detected_baud = BAUDS[0];
    summary += "Sa bajty, brak poprawnych ramek: baud spoza listy albo zla masa";
    Serial.println(
        "\nBytes arrive but no valid NMEA. Likely a baud rate outside the scan\n"
        "list, or a bad ground. Check GND is shared with the ESP32.");
  } else {
    detected_baud = BAUDS[0];
    summary +=
        "Nic na pinie RX. Kolejno: 1) zamienione TX/RX (TX modulu -> GPIO26) "
        "2) brak zasilania 3) brak wspolnej masy 4) modul zasilany z GPIO";
    Serial.println(
        "\nNothing on the RX pin. In order of likelihood:\n"
        "  1. TX and RX swapped: module TX must go to GPIO26, not GPIO27.\n"
        "  2. No power: measure VCC against GND at the module.\n"
        "  3. GND not shared between the module and the ESP32.\n"
        "  4. Module powered from a GPIO instead of the 5V/3V3 pin.");
  }

  scan_summary = summary;
  gpsSerial.updateBaudRate(detected_baud);
  Serial.println("\nLive view. Console: 'r' toggles raw NMEA, 's' rescans.");
}

String fixAgeText() {
  if (!gps.location.isValid()) return "brak";
  return String(gps.location.age() / 1000.0, 1) + " s temu";
}

String ttffText() {
  if (first_fix_ms == 0) return "jeszcze nie zlapany";
  return String((first_fix_ms - boot_ms) / 1000.0, 1) + " s od startu";
}

void handleRoot() {
  const bool has_fix = gps.location.isValid();
  String html;
  html.reserve(4096);
  html += F(
      "<!doctype html><html lang='pl'><head><meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<meta http-equiv='refresh' content='3'>"
      "<title>GPS probe</title><style>"
      "body{font-family:system-ui,sans-serif;margin:0;padding:16px;"
      "background:#12141a;color:#e8eaed}"
      "h1{font-size:17px;margin:0 0 12px}"
      "table{border-collapse:collapse;width:100%;max-width:640px}"
      "td{padding:7px 6px;border-bottom:1px solid #2a2e39;font-size:15px}"
      "td:first-child{color:#9aa0aa;width:46%}"
      ".ok{color:#5ad18b}.bad{color:#ff7b72}"
      "a{color:#6cb6ff}pre{background:#1b1e26;padding:10px;border-radius:6px;"
      "overflow-x:auto;font-size:13px;max-width:640px;white-space:pre-wrap}"
      "</style></head><body><h1>GPS probe</h1><table>");

  html += "<tr><td>Stan</td><td class='";
  html += has_fix ? "ok'>FIX" : "bad'>brak fixa";
  html += "</td></tr>";
  html += "<tr><td>Satelity</td><td>" +
          String(gps.satellites.isValid() ? gps.satellites.value() : 0) + " (maks. " +
          String(max_satellites) + ")</td></tr>";
  html += "<tr><td>HDOP</td><td>" +
          (gps.hdop.isValid() ? String(gps.hdop.hdop(), 1) : String("-")) +
          " (najlepszy " + (best_hdop < 99 ? String(best_hdop, 1) : String("-")) +
          ")</td></tr>";

  if (has_fix) {
    const double lat = gps.location.lat(), lon = gps.location.lng();
    html += "<tr><td>Pozycja</td><td>" + String(lat, 6) + ", " + String(lon, 6) +
            "</td></tr>";
    html += "<tr><td>Mapa</td><td><a target='_blank' "
            "href='https://www.openstreetmap.org/?mlat=" +
            String(lat, 6) + "&mlon=" + String(lon, 6) + "#map=17/" + String(lat, 6) +
            "/" + String(lon, 6) + "'>otworz</a></td></tr>";
    html += "<tr><td>Wysokosc</td><td>" +
            String(gps.altitude.isValid() ? gps.altitude.meters() : 0, 0) +
            " m</td></tr>";
    html += "<tr><td>Predkosc</td><td>" +
            String(gps.speed.isValid() ? gps.speed.kmph() : 0, 1) + " km/h</td></tr>";
  }

  html += "<tr><td>Wiek fixa</td><td>" + fixAgeText() + "</td></tr>";
  html += "<tr><td>Czas do pierwszego fixa</td><td>" + ttffText() + "</td></tr>";

  if (gps.date.isValid() && gps.time.isValid()) {
    char utc[32];
    snprintf(utc, sizeof(utc), "%04d-%02d-%02d %02d:%02d:%02d", gps.date.year(),
             gps.date.month(), gps.date.day(), gps.time.hour(), gps.time.minute(),
             gps.time.second());
    html += "<tr><td>UTC z GPS</td><td>" + String(utc) + "</td></tr>";
  }

  html += "<tr><td>Baud</td><td>" + String(detected_baud) + "</td></tr>";
  html += "<tr><td>Bajty / ramki OK / bledne</td><td>" + String(bytes_total) + " / " +
          String(gps.passedChecksum()) + " / " + String(gps.failedChecksum()) +
          "</td></tr>";
  html += "<tr><td>WiFi</td><td>" + wifi_state + ", " + String(WiFi.RSSI()) + " dBm, " +
          WiFi.localIP().toString() + "</td></tr>";
  html += "<tr><td>Uptime</td><td>" + String((millis() - boot_ms) / 1000) +
          " s</td></tr>";
  html += F("</table><h1>Skan portu</h1><pre>");
  html += scan_summary;
  html += F("</pre><p><a href='/rescan'>Powtorz skan</a> &middot; "
            "<a href='/json'>JSON</a></p></body></html>");

  server.send(200, "text/html; charset=utf-8", html);
}

void handleJson() {
  String j = "{";
  j += "\"fix\":" + String(gps.location.isValid() ? "true" : "false");
  if (gps.location.isValid()) {
    j += ",\"lat\":" + String(gps.location.lat(), 6);
    j += ",\"lon\":" + String(gps.location.lng(), 6);
    j += ",\"speed_kmh\":" + String(gps.speed.isValid() ? gps.speed.kmph() : 0, 1);
  }
  j += ",\"sat\":" + String(gps.satellites.isValid() ? gps.satellites.value() : 0);
  j += ",\"sat_max\":" + String(max_satellites);
  j += ",\"hdop\":" + String(gps.hdop.isValid() ? gps.hdop.hdop() : 99.0, 1);
  j += ",\"bytes\":" + String(bytes_total);
  j += ",\"nmea_ok\":" + String(gps.passedChecksum());
  j += ",\"nmea_bad\":" + String(gps.failedChecksum());
  j += ",\"baud\":" + String(detected_baud);
  j += ",\"ttff_s\":" + String(first_fix_ms ? (first_fix_ms - boot_ms) / 1000.0 : 0.0, 1);
  j += ",\"rssi\":" + String(WiFi.RSSI());
  j += ",\"uptime_s\":" + String((millis() - boot_ms) / 1000);
  j += "}";
  server.send(200, "application/json", j);
}

void handleRescan() {
  server.sendHeader("Location", "/");
  server.send(303);
  scan();
}

void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);  // latency matters more here than the few mA saved
  WiFi.setHostname(MDNS_NAME);
  WiFi.begin(PROBE_WIFI_SSID, PROBE_WIFI_PASS);
  Serial.printf("WiFi: laczenie z %s", PROBE_WIFI_SSID);

  const uint32_t deadline = millis() + 25000;
  while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    // Not fatal: the probe still works over USB, so say so instead of hanging.
    wifi_state = "brak polaczenia";
    Serial.println("WiFi: nie polaczono. Probe dziala dalej przez USB.");
    return;
  }

  wifi_state = "polaczono";
  Serial.printf("WiFi: %s  IP %s  RSSI %d dBm\n", PROBE_WIFI_SSID,
                WiFi.localIP().toString().c_str(), WiFi.RSSI());
  if (MDNS.begin(MDNS_NAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("mDNS: http://%s.local/\n", MDNS_NAME);
  }
  Serial.printf("Otworz: http://%s/\n", WiFi.localIP().toString().c_str());
}

void setup() {
  Serial.begin(115200);
  delay(300);
  boot_ms = millis();
  pinMode(PIN_LED, OUTPUT);

  gpsSerial.begin(BAUDS[0], SERIAL_8N1, PIN_GNSS_RX, PIN_GNSS_TX);
  // An idle UART line sits high. Without the pull-up a disconnected RX pin
  // floats and delivers noise bytes, which reads exactly like "module present
  // but wrong baud rate". With it, "nothing connected" means zero bytes.
  pinMode(PIN_GNSS_RX, INPUT_PULLUP);

  connectWifi();

  server.on("/", handleRoot);
  server.on("/json", handleJson);
  server.on("/rescan", handleRescan);
  server.begin();

  scan();
  last_report = millis();
}

void loop() {
  server.handleClient();

  // WiFi can drop; reconnect without blocking the GPS reader.
  static uint32_t last_wifi_check = 0;
  if (millis() - last_wifi_check > 10000) {
    last_wifi_check = millis();
    if (WiFi.status() != WL_CONNECTED) {
      wifi_state = "rozlaczono, ponawiam";
      WiFi.reconnect();
    } else if (wifi_state != "polaczono") {
      wifi_state = "polaczono";
    }
  }

  while (Serial.available()) {
    const char c = Serial.read();
    if (c == 'r') {
      echo_raw = !echo_raw;
      Serial.printf("\n[raw echo %s]\n", echo_raw ? "on" : "off");
    } else if (c == 's') {
      scan();
    }
  }

  while (gpsSerial.available()) {
    const char c = gpsSerial.read();
    bytes_total++;
    gps.encode(c);
    if (echo_raw) Serial.write(c);
  }

  if (gps.location.isValid()) {
    if (first_fix_ms == 0) first_fix_ms = millis();
    digitalWrite(PIN_LED, HIGH);
    if (gps.satellites.isValid() && gps.satellites.value() > max_satellites) {
      max_satellites = gps.satellites.value();
    }
    if (gps.hdop.isValid() && gps.hdop.hdop() < best_hdop) best_hdop = gps.hdop.hdop();
  } else {
    // Slow blink while searching, so the board says something with no console.
    digitalWrite(PIN_LED, (millis() / 500) % 2);
  }

  if (millis() - last_report < 5000) return;
  last_report = millis();

  Serial.println("------------------------------------------------------------");
  Serial.printf("bytes: %lu   sentences ok: %lu   checksum fail: %lu\n", bytes_total,
                gps.passedChecksum(), gps.failedChecksum());
  Serial.printf("satellites: %s   hdop: %s   wifi: %s %s\n",
                gps.satellites.isValid() ? String(gps.satellites.value()).c_str() : "-",
                gps.hdop.isValid() ? String(gps.hdop.hdop(), 1).c_str() : "-",
                wifi_state.c_str(), WiFi.localIP().toString().c_str());

  if (gps.location.isValid()) {
    Serial.printf("FIX: %.6f, %.6f   alt %.0f m   speed %.1f km/h   age %lu ms\n",
                  gps.location.lat(), gps.location.lng(),
                  gps.altitude.isValid() ? gps.altitude.meters() : 0.0,
                  gps.speed.isValid() ? gps.speed.kmph() : 0.0, gps.location.age());
  } else {
    Serial.println("FIX: none yet");
  }
  Serial.println();
}
