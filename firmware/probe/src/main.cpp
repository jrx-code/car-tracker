// GPS bring-up probe.
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
// Wiring (matches firmware/include/pins.h):
//   NEO-6M TX  -> ESP32 GPIO26
//   NEO-6M RX  -> ESP32 GPIO27
//   NEO-6M VCC -> ESP32 5V (module with an onboard regulator) or 3V3
//   NEO-6M GND -> ESP32 GND
#include <Arduino.h>
#include <TinyGPSPlus.h>

constexpr int PIN_GNSS_RX = 26;  // ESP32 receives here, module TX goes here
constexpr int PIN_GNSS_TX = 27;  // ESP32 transmits here, module RX goes here

// 9600 is the u-blox default; clones are sometimes shipped at 38400.
constexpr uint32_t BAUDS[] = {9600, 38400, 115200, 4800, 57600};
constexpr size_t BAUD_COUNT = sizeof(BAUDS) / sizeof(BAUDS[0]);
constexpr uint32_t SCAN_MS = 3000;

HardwareSerial gpsSerial(1);
TinyGPSPlus gps;

uint32_t detected_baud = 0;
uint32_t bytes_total = 0;
uint32_t last_report = 0;
bool echo_raw = true;

// Try one baud rate and report how many bytes and how many valid NMEA
// sentences arrive within the window.
struct ScanResult {
  uint32_t bytes;
  uint32_t sentences;
};

ScanResult tryBaud(uint32_t baud) {
  // updateBaudRate, never begin/end per attempt: HardwareSerial::end() detaches
  // every pin and deletes the UART driver, and re-begin()ing UART1 in a loop
  // produced a flood of repeated output on the USB console. One begin() in
  // setup(), rate changes only, keeps the port stable.
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
    delay(1);
  }
  r.sentences = probe.passedChecksum();
  return r;
}

void scan() {
  Serial.println();
  Serial.println("=== GPS probe: scanning baud rates ===");
  Serial.printf("RX pin GPIO%d (module TX), TX pin GPIO%d (module RX)\n",
                PIN_GNSS_RX, PIN_GNSS_TX);

  uint32_t best_baud = 0;
  uint32_t best_sentences = 0;
  uint32_t any_bytes = 0;

  for (size_t i = 0; i < BAUD_COUNT; i++) {
    const ScanResult r = tryBaud(BAUDS[i]);
    any_bytes += r.bytes;
    Serial.printf("  %6lu baud: %5lu bytes, %3lu valid NMEA sentences\n",
                  BAUDS[i], r.bytes, r.sentences);
    if (r.sentences > best_sentences) {
      best_sentences = r.sentences;
      best_baud = BAUDS[i];
    }
  }

  if (best_baud != 0) {
    detected_baud = best_baud;
    Serial.printf("\nOK: receiver talking at %lu baud.\n", detected_baud);
  } else if (any_bytes > 0) {
    detected_baud = BAUDS[0];
    Serial.println(
        "\nBytes arrive but no valid NMEA. Likely a baud rate outside the scan\n"
        "list, or a bad ground. Check GND is shared with the ESP32.");
  } else {
    detected_baud = BAUDS[0];
    Serial.println(
        "\nNothing on the RX pin. In order of likelihood:\n"
        "  1. TX and RX swapped: module TX must go to GPIO26, not GPIO27.\n"
        "  2. No power: measure VCC against GND at the module.\n"
        "  3. GND not shared between the module and the ESP32.\n"
        "  4. Module powered from a GPIO instead of the 5V/3V3 pin.");
  }

  gpsSerial.updateBaudRate(detected_baud);
  Serial.println("\nLive view. Send 'r' to toggle the raw NMEA echo, 's' to rescan.");
  Serial.println("A cold start under an open sky takes about 30 s on a NEO-6M,\n"
                 "and can take several minutes indoors or with no sky view.\n");
}

void setup() {
  Serial.begin(115200);
  delay(300);
  gpsSerial.begin(BAUDS[0], SERIAL_8N1, PIN_GNSS_RX, PIN_GNSS_TX);
  // An idle UART line sits high. Without the pull-up a disconnected RX pin
  // floats and delivers noise bytes, which reads exactly like "module present
  // but wrong baud rate". With it, "nothing connected" means zero bytes.
  pinMode(PIN_GNSS_RX, INPUT_PULLUP);
  scan();
  last_report = millis();
}

void loop() {
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

  if (millis() - last_report < 5000) return;
  last_report = millis();

  Serial.println("------------------------------------------------------------");
  Serial.printf("bytes: %lu   sentences ok: %lu   checksum fail: %lu\n",
                bytes_total, gps.passedChecksum(), gps.failedChecksum());
  Serial.printf("satellites: %s   hdop: %s\n",
                gps.satellites.isValid() ? String(gps.satellites.value()).c_str() : "-",
                gps.hdop.isValid() ? String(gps.hdop.hdop(), 1).c_str() : "-");

  if (gps.location.isValid()) {
    Serial.printf("FIX: %.6f, %.6f   alt %.0f m   speed %.1f km/h   age %lu ms\n",
                  gps.location.lat(), gps.location.lng(),
                  gps.altitude.isValid() ? gps.altitude.meters() : 0.0,
                  gps.speed.isValid() ? gps.speed.kmph() : 0.0,
                  gps.location.age());
    Serial.printf("map: https://maps.google.com/?q=%.6f,%.6f\n", gps.location.lat(),
                  gps.location.lng());
  } else {
    Serial.println("FIX: none yet");
  }

  if (gps.date.isValid() && gps.time.isValid()) {
    Serial.printf("UTC: %04d-%02d-%02d %02d:%02d:%02d\n", gps.date.year(),
                  gps.date.month(), gps.date.day(), gps.time.hour(),
                  gps.time.minute(), gps.time.second());
  }
  Serial.println();
}
