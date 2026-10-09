// Copy to secrets.h and fill in. secrets.h is gitignored.
// Use the SSID of the network the board will actually join.
#pragma once

#define PROBE_WIFI_SSID "IoT-SSID"
#define PROBE_WIFI_PASS "REPLACE_ME"

// Access point the probe opens itself when it cannot join the network above
// (in the car, away from home). WPA2 needs a password of 8 to 63 characters.
#define PROBE_AP_SSID "REPLACE_ME"
#define PROBE_AP_PASS "REPLACE_ME"
