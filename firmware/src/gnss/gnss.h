// GNSS layer. Two possible sources: the external NEO-6M on a UART and, on
// variants where the modem has its own receiver, the modem. Which one wins is
// decided by the gnss_src config key (docs/05 section 5.6).
#pragma once
#include <Arduino.h>

#include "state.h"

namespace gnss {

void begin();

// Power up the receiver and start feeding NMEA. Cold start takes about 27 s on
// the NEO-6M, which is a GPS-only receiver (docs/03 section 3.1).
void enable();
void disable();
bool enabled();

// Pump the UART. Call often, it is non blocking.
void poll();

// Fill a record from the last valid fix. Returns false when there is no fix or
// when HDOP is worse than hdop_max.
bool fill(PosRecord& out, float hdop_max);

bool hasFix();
uint8_t satellites();
float hdop();
uint32_t utc();  // 0 when the receiver has no time yet
uint32_t msSinceFix();

// Feed a fix obtained from the modem GNSS instead of the UART receiver, so both
// sources end up in the same picture and the src field says which one it was.
void feedModemFix(double lat, double lon, float speed_kmh, float course,
                  float alt, int sats, float hdop, uint32_t utc_ts);

}  // namespace gnss
