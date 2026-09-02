// Minimal LIS3DH driver: low power mode plus INT1 motion detection.
// Written by hand instead of pulling a sensor framework in, because the whole
// point of this chip here is the microamp-level standby current and that is
// controlled by a handful of registers.
#pragma once
#include <Arduino.h>

namespace motion {

// sensitivity 1..5, maps to the INT1 threshold register (higher = more sensitive)
bool begin(uint8_t sensitivity);

// Reconfigure the threshold without a full re-init (arrives from the cfg topic).
void setSensitivity(uint8_t sensitivity);

// True while the interrupt line is asserted. Reading the source register also
// clears the latch.
bool interruptActive();
bool readAndClear();

// Vector magnitude in g, for logging and for tuning the threshold from data
// instead of from guesswork.
float magnitude();

// Sustained movement detector: true once movement has been continuous for
// hold_ms. Used for the driving/parked decision (docs/02 section 2.4).
bool sustainedMotion(uint32_t hold_ms);

}  // namespace motion
