// On-device web portal: live status for anyone, settings behind an admin mode.
//
// Reachable two ways, and the second one is the point:
//   1. over the normal WiFi network, for the bench and the garage;
//   2. over an emergency access point the device raises itself when it has been
//      offline for too long. Without that, a tracker under a dashboard with a
//      wrong WiFi password or a dead SIM has no way in at all.
#pragma once
#include <Arduino.h>
#include <functional>

namespace portal {

// The portal has no opinion about what the tracker is doing; main.cpp owns the
// vehicle state and hands over a JSON snapshot for the status page.
using StatusProvider = std::function<String()>;
void setStatusProvider(StatusProvider provider);

void begin();

// Pump the HTTP server and run the connectivity state machine (join WiFi, fall
// back to the AP, retire the AP again). Call from loop().
void loop();

// True while the emergency AP is up.
bool apActive();

// True when the station interface is associated.
bool staConnected();

String ipAddress();

// Bring the AP up or down by hand, from a command or from the portal.
void startAp();
void stopAp();

}  // namespace portal
