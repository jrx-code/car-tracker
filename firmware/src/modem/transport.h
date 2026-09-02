// Transport abstraction: "publish these bytes on this topic". It does not know
// whether the link underneath is LTE or WiFi, which is what lets the modem
// decision stay open until the parts are bought (docs/03).
#pragma once
#include <Arduino.h>
#include <functional>

namespace transport {

using MessageHandler =
    std::function<void(const char* topic, const uint8_t* payload, unsigned len)>;

struct LinkInfo {
  int16_t rssi = 0;
  bool roaming = false;
  char net[8] = "";     // WIFI, LTE, LTE-M, NB, GSM
  char oper[12] = "";   // operator code, empty on WiFi
  char imei[20] = "";
  char iccid[24] = "";
};

// Bring the hardware up. Does not connect yet.
bool begin();

// Attach to the network and to the broker. Blocking, with an internal timeout.
bool connect(const char* client_id, const char* user, const char* pass,
             const char* lwt_topic, const char* lwt_payload);

bool connected();
void loop();
bool publish(const char* topic, const char* payload, bool retain);
bool subscribe(const char* topic);
void onMessage(MessageHandler handler);

// Put the link to sleep and cut the modem rail. After this, connect() has to
// run through the full attach again, which is exactly the trade we want in
// PARKED: a slower wake-up in exchange for the current budget (docs/04).
void sleep();

const LinkInfo& info();

// UTC from the network, used when there is no GNSS fix (docs/02 section 2.7).
uint32_t networkTime();

// Ask the modem for its own GNSS fix. Returns false on variants where the
// modem has no receiver (MODEM_HAS_GNSS == 0) or when there is no fix.
bool modemGnssFix(double& lat, double& lon, float& speed_kmh, float& course,
                  float& alt, int& sats, float& hdop, uint32_t& utc_ts);

}  // namespace transport
