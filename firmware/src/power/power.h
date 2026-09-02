// Battery voltage measurement, load switches and sleep.
// This module reports facts and executes orders. It never decides to sleep by
// itself; main.cpp owns that decision (docs/02 section 2.2).
#pragma once
#include <Arduino.h>

namespace power {

void begin();

// Averaged battery voltage from the OBD pin 16 divider, in volts.
// Uses the two-point calibration stored in NVS, not a theoretical formula,
// because the ESP32 ADC is not linear (docs/04 section 4.4).
float readVbat();

// Supply rail of the ESP32 itself, useful to tell a brownout from a reset.
float readVsys();

// Two-point calibration: measure with a bench supply at two known voltages.
void setCalibration(float raw_lo, float v_lo, float raw_hi, float v_hi);
float rawAdcAvg();

// Load switches. Cutting the rail, not sleeping the chip, is what keeps the
// parked current under the budget in docs/04 section 4.2.
void modemPower(bool on);
void gnssPower(bool on);
bool modemPowered();
bool gnssPowered();

// Deep sleep for the given number of seconds. Wakes early on the accelerometer
// interrupt if wake_on_motion is true.
void deepSleep(uint32_t seconds, bool wake_on_motion);

// Reason for the last reset, as reported by the ESP-IDF API. Sent in telemetry
// so that a brownout is distinguishable from a watchdog reset.
uint8_t resetReason();

}  // namespace power
