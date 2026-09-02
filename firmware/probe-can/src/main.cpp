// CAN sniffing probe.
//
// Answers, in this order, the questions that actually decide whether phase 2
// is possible at all (docs/06-can-obd.md, steps K1 to K3):
//   1. Which pair of OBD pins carries a bus?            (move the two wires)
//   2. At which bit rate?                               (automatic scan)
//   3. Which IDs appear, how often, how long?           (per-ID table)
//   4. Which bytes inside an ID actually move?          (per-byte change mask)
//   5. What did a byte do during a drive?               (per-ID change log)
//
// Question 4 is the one that matters. A raw dump of a 500 kb/s bus is thousands
// of frames a second and nobody reads that. What finds fuel level or coolant
// temperature is knowing that of ID 0x4xx only byte 3 ever moves, and moves
// slowly. The table below is built to make that visible without a laptop.
//
// EVERYTHING HERE IS LISTEN ONLY. The controller is installed in
// TWAI_MODE_LISTEN_ONLY, which does not even send the acknowledge bit, so a
// wrong bit rate produces receive errors here and silence on the car's bus.
// That is what makes the bit rate scan safe to run on somebody else's vehicle.
//
// Wiring:
//   transceiver CANH  -> OBD pin 6      [TO CONFIRM, docs/06 section 6.2]
//   transceiver CANL  -> OBD pin 14     [TO CONFIRM]
//   transceiver GND   -> OBD pin 4 and ESP32 GND
//   transceiver VCC   -> ESP32 3V3      (SN65HVD230/231 are 3.3 V parts)
//   transceiver R/RXD -> ESP32 GPIO18
//   transceiver D/TXD -> ESP32 3V3, NOT to a GPIO. See below.
//   transceiver RS    -> ESP32 GPIO32 if the board brings it out, else leave it
//
// On a Waveshare SN65HVD230 CAN Board the header is CAN_TX, GND, 3.3V, CAN_RX
// and RS is not on it, so "D to VCC" is a jumper between two header pins and
// GPIO32 stays unused. Its R2 (120 ohm) has to come off the board first.
//   power             -> power bank on USB. NOT OBD pin 16.
//
// D IS TIED HIGH ON PURPOSE. On the SN65HVD230 the driver input is low for
// dominant and high for recessive, so a D held at VCC means the output stage
// physically cannot pull the bus down, whatever the controller, the firmware or
// a stray reset does. TWAI is still installed in listen-only and still never
// asserts TX, so this is the second of two independent guarantees rather than a
// replacement for the first. It also removes any dependence on the RS pin,
// which cheap breakout boards often do not bring out to the header.
//
// TWAI insists on being given a TX pin, so it gets one that goes nowhere.
//
// The 120 ohm terminator on a breakout board must be removed first: the car's
// bus is already terminated at both ends and a third resistor drops the line
// impedance to 60 ohms. Measure between CANH and CANL on the unplugged module:
// open circuit is what you want, 120 ohms means find the resistor or the jumper.
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <driver/twai.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>

#include "secrets.h"

// Deliberately not wired to the transceiver: the driver input is held at VCC in
// hardware. TWAI requires a TX pin, so it drives a pin with nothing on it.
constexpr gpio_num_t PIN_CAN_TX = GPIO_NUM_19;
constexpr gpio_num_t PIN_CAN_RX = GPIO_NUM_18;
constexpr int PIN_CAN_RS = 32;  // only if the board brings RS out; LOW = normal
constexpr int PIN_LED = 2;
constexpr const char* MDNS_NAME = "can-probe";

// 500 first because a 2016 car has to answer OBD-II on a 500 kb/s bus, and 125
// second because that is what the medium speed bus in Mazdas is reported to
// run at. The rest are here so a silent bus is not mistaken for a missing one.
constexpr uint32_t RATES[] = {500000, 125000, 250000, 1000000, 100000, 50000};
constexpr size_t RATE_COUNT = sizeof(RATES) / sizeof(RATES[0]);
constexpr uint32_t SCAN_MS = 4000;

constexpr size_t MAX_IDS = 96;      // more distinct IDs than an MX-5 should show
constexpr size_t LOG_DEPTH = 240;   // change log entries for the watched ID

struct IdStats {
  uint32_t id;
  uint8_t dlc;
  uint32_t count;
  uint32_t first_ms;
  uint32_t last_ms;
  uint8_t last[8];
  uint8_t changed;      // bit per byte: this byte was ever seen to change
  uint8_t lo[8];
  uint8_t hi[8];
};

struct LogEntry {
  uint32_t ms;
  uint8_t dlc;
  uint8_t data[8];
};

IdStats ids[MAX_IDS];
size_t id_count = 0;

LogEntry change_log[LOG_DEPTH];
size_t log_head = 0;
size_t log_used = 0;
uint32_t watched_id = 0xFFFFFFFF;  // none

WebServer server(80);

uint32_t boot_ms = 0;
uint32_t capture_start_ms = 0;
uint32_t detected_rate = 0;
uint32_t frames_total = 0;
bool driver_up = false;
bool scan_running = false;
String scan_summary = "nie uruchomiony";
String wifi_state = "rozlaczony";

// --- driver -----------------------------------------------------------------

void canStop() {
  if (!driver_up) return;
  twai_stop();
  twai_driver_uninstall();
  driver_up = false;
}

bool canStart(uint32_t bitrate) {
  canStop();

  twai_timing_config_t timing;
  switch (bitrate) {
    case 1000000: timing = TWAI_TIMING_CONFIG_1MBITS(); break;
    case 500000:  timing = TWAI_TIMING_CONFIG_500KBITS(); break;
    case 250000:  timing = TWAI_TIMING_CONFIG_250KBITS(); break;
    case 125000:  timing = TWAI_TIMING_CONFIG_125KBITS(); break;
    case 100000:  timing = TWAI_TIMING_CONFIG_100KBITS(); break;
    case 50000:   timing = TWAI_TIMING_CONFIG_50KBITS(); break;
    default: return false;
  }

  // LISTEN_ONLY, not NORMAL and not NO_ACK: the controller must never drive the
  // bus, not even to acknowledge a frame it understood. This is the whole
  // safety argument for probing a car we do not own the bus of.
  twai_general_config_t general =
      TWAI_GENERAL_CONFIG_DEFAULT(PIN_CAN_TX, PIN_CAN_RX, TWAI_MODE_LISTEN_ONLY);
  general.rx_queue_len = 64;
  general.tx_queue_len = 0;  // nothing is ever transmitted

  twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  if (twai_driver_install(&general, &timing, &filter) != ESP_OK) return false;
  if (twai_start() != ESP_OK) {
    twai_driver_uninstall();
    return false;
  }
  driver_up = true;
  return true;
}

void resetStats() {
  id_count = 0;
  frames_total = 0;
  log_head = 0;
  log_used = 0;
  capture_start_ms = millis();
}

// --- capture ----------------------------------------------------------------

IdStats* findOrAdd(uint32_t id) {
  for (size_t i = 0; i < id_count; i++) {
    if (ids[i].id == id) return &ids[i];
  }
  if (id_count >= MAX_IDS) return nullptr;
  IdStats& s = ids[id_count++];
  memset(&s, 0, sizeof(s));
  s.id = id;
  s.first_ms = millis();
  memset(s.lo, 0xFF, sizeof(s.lo));
  memset(s.hi, 0x00, sizeof(s.hi));
  return &s;
}

void record(const twai_message_t& msg) {
  frames_total++;
  IdStats* s = findOrAdd(msg.identifier);
  if (!s) return;

  const uint8_t dlc = msg.data_length_code > 8 ? 8 : msg.data_length_code;
  for (uint8_t i = 0; i < dlc; i++) {
    const uint8_t b = msg.data[i];
    if (s->count && b != s->last[i]) s->changed |= (1 << i);
    if (b < s->lo[i]) s->lo[i] = b;
    if (b > s->hi[i]) s->hi[i] = b;
  }

  const bool differs = s->count == 0 || dlc != s->dlc ||
                       memcmp(s->last, msg.data, dlc) != 0;

  s->dlc = dlc;
  memcpy(s->last, msg.data, dlc);
  s->count++;
  s->last_ms = millis();

  // The log keeps changes only. A frame repeating the same payload at 100 Hz
  // says nothing that the rate column does not already say.
  if (msg.identifier == watched_id && differs) {
    LogEntry& e = change_log[log_head];
    e.ms = s->last_ms;
    e.dlc = dlc;
    memcpy(e.data, msg.data, dlc);
    log_head = (log_head + 1) % LOG_DEPTH;
    if (log_used < LOG_DEPTH) log_used++;
  }
}

void pump(uint32_t budget_ms) {
  const uint32_t until = millis() + budget_ms;
  twai_message_t msg;
  while (millis() < until) {
    if (twai_receive(&msg, 0) != ESP_OK) break;
    if (!msg.rtr) record(msg);
  }
}

// --- bit rate scan ----------------------------------------------------------

void runScan() {
  scan_running = true;
  scan_summary = "";
  uint32_t best_rate = 0;
  uint32_t best_frames = 0;

  for (size_t i = 0; i < RATE_COUNT; i++) {
    const uint32_t rate = RATES[i];
    if (!canStart(rate)) {
      scan_summary += String(rate / 1000) + " kb/s: sterownik nie wstal\n";
      continue;
    }
    uint32_t seen = 0;
    twai_message_t msg;
    const uint32_t until = millis() + SCAN_MS;
    while (millis() < until) {
      if (twai_receive(&msg, pdMS_TO_TICKS(50)) == ESP_OK) seen++;
    }
    twai_status_info_t st;
    twai_get_status_info(&st);

    scan_summary += String(rate / 1000) + " kb/s: ramek " + String(seen) +
                    ", bledy RX " + String(st.rx_error_counter) +
                    ", zgubione " + String(st.rx_missed_count) + "\n";
    if (seen > best_frames) {
      best_frames = seen;
      best_rate = rate;
    }
    canStop();
  }

  if (best_rate) {
    detected_rate = best_rate;
    scan_summary += "\nWybrano " + String(best_rate / 1000) + " kb/s.\n";
    canStart(best_rate);
    resetStats();
  } else {
    detected_rate = 0;
    scan_summary +=
        "\nZadna predkosc nie dala ramek. Sprawdz w tej kolejnosci:\n"
        "1. Czy CANH i CANL nie sa zamienione.\n"
        "2. Czy masa transceivera jest na pinie 4 gniazda.\n"
        "3. Czy magistrala jest na tej parze pinow (sprobuj 3 i 11).\n"
        "4. Czy zaplon jest wlaczony. Przy spiacym aucie magistrala milczy,\n"
        "   i to jest wynik sam w sobie: znaczy, ze moduly zasnely.\n";
  }
  scan_running = false;
}

// --- formatting -------------------------------------------------------------

String hex2(uint8_t v) {
  char b[3];
  snprintf(b, sizeof(b), "%02X", v);
  return String(b);
}

String hexId(uint32_t v) {
  char b[12];
  snprintf(b, sizeof(b), "0x%03X", (unsigned)v);
  return String(b);
}

// Frames per second, computed over the window this ID was actually present for
// rather than over the whole capture: an ID that only appears while driving
// would otherwise look ten times rarer than it is.
float rateHz(const IdStats& s) {
  const uint32_t span = s.last_ms - s.first_ms;
  if (span < 500 || s.count < 2) return 0.0f;
  return (s.count - 1) * 1000.0f / span;
}

int idOrder(const void* a, const void* b) {
  const IdStats* x = static_cast<const IdStats*>(a);
  const IdStats* y = static_cast<const IdStats*>(b);
  return (x->id > y->id) - (x->id < y->id);
}

// --- pages ------------------------------------------------------------------

const char kStyle[] PROGMEM =
    "<style>body{font-family:system-ui,sans-serif;background:#12141a;color:#e8eaed;"
    "margin:0;padding:14px}h1{font-size:15px;margin:18px 0 8px}"
    "table{border-collapse:collapse;width:100%;font-size:13px}"
    "td,th{border-bottom:1px solid #2c313f;padding:5px 7px;text-align:left}"
    "th{color:#98a0ae;font-weight:600}code,pre{font-family:ui-monospace,monospace}"
    "pre{background:#1a1d26;padding:10px;border-radius:6px;overflow-x:auto}"
    ".m{color:#5ad18b;font-weight:600}.s{color:#3d4351}"
    "a{color:#6cb6ff}</style>";

void handleRoot() {
  qsort(ids, id_count, sizeof(IdStats), idOrder);

  String h = F("<!doctype html><meta charset='utf-8'>"
               "<meta name=viewport content='width=device-width,initial-scale=1'>"
               "<title>Sonda CAN</title>");
  h += FPSTR(kStyle);
  h += F("<h1>Stan</h1><table>");
  h += "<tr><td>Predkosc</td><td>" +
       (detected_rate ? String(detected_rate / 1000) + " kb/s" : String("nieustalona")) +
       "</td></tr>";
  h += "<tr><td>Tryb</td><td>listen only, nic nie jest wysylane</td></tr>";
  h += "<tr><td>Ramek / roznych ID</td><td>" + String(frames_total) + " / " +
       String(id_count) + "</td></tr>";
  h += "<tr><td>Czas zbierania</td><td>" +
       String(capture_start_ms ? (millis() - capture_start_ms) / 1000 : 0) + " s</td></tr>";

  if (driver_up) {
    twai_status_info_t st;
    twai_get_status_info(&st);
    h += "<tr><td>Bledy RX / zgubione</td><td>" + String(st.rx_error_counter) + " / " +
         String(st.rx_missed_count) + "</td></tr>";
  }
  h += "<tr><td>WiFi</td><td>" + wifi_state + ", " + String(WiFi.RSSI()) + " dBm, " +
       WiFi.localIP().toString() + "</td></tr>";
  h += "<tr><td>Uptime</td><td>" + String((millis() - boot_ms) / 1000) + " s</td></tr>";
  h += F("</table>");

  h += F("<h1>Skan predkosci</h1><pre>");
  h += scan_running ? String("trwa...") : scan_summary;
  h += F("</pre>");

  h += F("<h1>Identyfikatory</h1>"
         "<p class=s>Zielone bajty zmienialy sie w trakcie zbierania. Szare byly "
         "stale. Kandydatow na paliwo i temperature szukaj wsrod bajtow zmiennych "
         "o niskiej czestotliwosci ramki.</p>"
         "<table><tr><th>ID</th><th>Hz</th><th>Ramek</th><th>Bajty (min-max)</th>"
         "<th></th></tr>");
  for (size_t i = 0; i < id_count; i++) {
    const IdStats& s = ids[i];
    h += "<tr><td><code>" + hexId(s.id) + "</code></td><td>" +
         String(rateHz(s), 1) + "</td><td>" + String(s.count) + "</td><td><code>";
    for (uint8_t b = 0; b < s.dlc; b++) {
      const bool moved = s.changed & (1 << b);
      h += String(moved ? "<span class=m>" : "<span class=s>") + hex2(s.last[b]);
      if (moved) h += "<sub>" + hex2(s.lo[b]) + "-" + hex2(s.hi[b]) + "</sub>";
      h += "</span> ";
    }
    h += "</code></td><td><a href='/watch?id=" + String(s.id) + "'>sledz</a></td></tr>";
  }
  h += F("</table>");

  if (watched_id != 0xFFFFFFFF) {
    h += "<h1>Zmiany w " + hexId(watched_id) + "</h1><pre>";
    for (size_t i = 0; i < log_used; i++) {
      const size_t idx = (log_head + LOG_DEPTH - log_used + i) % LOG_DEPTH;
      const LogEntry& e = change_log[idx];
      h += String(e.ms / 1000.0f, 2) + " s  ";
      for (uint8_t b = 0; b < e.dlc; b++) h += hex2(e.data[b]) + " ";
      h += "\n";
    }
    if (!log_used) h += "brak zmian\n";
    h += F("</pre>");
  }

  h += F("<p><a href='/scan'>Skan predkosci</a> &middot; "
         "<a href='/reset'>Wyczysc statystyki</a> &middot; "
         "<a href='/dump.csv'>Pobierz CSV</a> &middot; "
         "<a href='/'>Odswiez</a></p>");
  server.send(200, "text/html; charset=utf-8", h);
}

// CSV rather than JSON: this file is opened in a spreadsheet or fed to a script
// on the way home, and every tool reads CSV without asking questions.
void handleDump() {
  String csv = F("id_hex,id_dec,dlc,count,hz,changed_mask,last,lo,hi\n");
  for (size_t i = 0; i < id_count; i++) {
    const IdStats& s = ids[i];
    csv += hexId(s.id) + "," + String(s.id) + "," + String(s.dlc) + "," +
           String(s.count) + "," + String(rateHz(s), 2) + "," + hex2(s.changed) + ",";
    for (uint8_t b = 0; b < s.dlc; b++) csv += hex2(s.last[b]);
    csv += ",";
    for (uint8_t b = 0; b < s.dlc; b++) csv += hex2(s.lo[b]);
    csv += ",";
    for (uint8_t b = 0; b < s.dlc; b++) csv += hex2(s.hi[b]);
    csv += "\n";
  }
  server.sendHeader("Content-Disposition", "attachment; filename=can.csv");
  server.send(200, "text/csv", csv);
}

void handleWatch() {
  if (server.hasArg("id")) {
    watched_id = server.arg("id").toInt();
    log_head = 0;
    log_used = 0;
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleReset() {
  resetStats();
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleScan() {
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "skan uruchomiony");
  runScan();
}

// --- setup ------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(300);
  boot_ms = millis();

  pinMode(PIN_LED, OUTPUT);
  // RS low = normal mode on SN65HVD230/231, which is what we want for sniffing:
  // the receiver has to stay awake. Harmless if the breakout does not bring RS
  // out, in which case the board has already fixed it on the PCB.
  pinMode(PIN_CAN_RS, OUTPUT);
  digitalWrite(PIN_CAN_RS, LOW);

  Serial.println("\nSonda CAN. Tryb listen only, nic nie jest wysylane na magistrale.");

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setHostname(MDNS_NAME);
  WiFi.begin(PROBE_WIFI_SSID, PROBE_WIFI_PASS);
  const uint32_t deadline = millis() + 20000;
  while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
    delay(250);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    wifi_state = "polaczony";
    MDNS.begin(MDNS_NAME);
    ArduinoOTA.setHostname(MDNS_NAME);
    ArduinoOTA.begin();
    Serial.printf("\nOtworz: http://%s/  albo http://%s.local/\n",
                  WiFi.localIP().toString().c_str(), MDNS_NAME);
  } else {
    Serial.println("\nWiFi: nie polaczono. Sonda dziala dalej przez USB.");
  }

  server.on("/", handleRoot);
  server.on("/scan", handleScan);
  server.on("/reset", handleReset);
  server.on("/watch", handleWatch);
  server.on("/dump.csv", handleDump);
  server.begin();

  resetStats();
  runScan();
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  if (driver_up) pump(5);
  digitalWrite(PIN_LED, (millis() / 500) % 2);
}
