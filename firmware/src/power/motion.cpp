#include "motion.h"

#include <Wire.h>

#include "pins.h"

namespace motion {
namespace {

constexpr uint8_t kAddrPrimary = 0x18;
constexpr uint8_t kAddrAlt = 0x19;

constexpr uint8_t REG_WHO_AM_I = 0x0F;
constexpr uint8_t REG_CTRL1 = 0x20;
constexpr uint8_t REG_CTRL2 = 0x21;
constexpr uint8_t REG_CTRL3 = 0x22;
constexpr uint8_t REG_CTRL4 = 0x23;
constexpr uint8_t REG_CTRL5 = 0x24;
constexpr uint8_t REG_OUT_X_L = 0x28;
constexpr uint8_t REG_INT1_CFG = 0x30;
constexpr uint8_t REG_INT1_SRC = 0x31;
constexpr uint8_t REG_INT1_THS = 0x32;
constexpr uint8_t REG_INT1_DUR = 0x33;

constexpr uint8_t WHO_AM_I_LIS3DH = 0x33;

uint8_t addr = kAddrPrimary;
bool present = false;
uint32_t motion_since_ms = 0;

void w8(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

uint8_t r8(uint8_t reg) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0;
  if (Wire.requestFrom(addr, static_cast<uint8_t>(1)) != 1) return 0;
  return Wire.read();
}

// Threshold register, 1 LSB = 16 mg at the +/-2 g full scale.
// [TO TUNE from measurements: pick the value from a recorded distribution of
//  door slams versus real towing, not from a guess (see the threshold rule in
//  docs/02 section 2.4).]
uint8_t thresholdFor(uint8_t sensitivity) {
  switch (sensitivity) {
    case 1: return 40;  // ~640 mg
    case 2: return 24;  // ~384 mg
    case 3: return 16;  // ~256 mg, default
    case 4: return 10;  // ~160 mg
    default: return 6;  // ~96 mg
  }
}

}  // namespace

bool begin(uint8_t sensitivity) {
  Wire.begin(PIN_ACC_SDA, PIN_ACC_SCL);
  Wire.setClock(100000);

  addr = kAddrPrimary;
  if (r8(REG_WHO_AM_I) != WHO_AM_I_LIS3DH) {
    addr = kAddrAlt;
    if (r8(REG_WHO_AM_I) != WHO_AM_I_LIS3DH) {
      present = false;
      return false;
    }
  }
  present = true;

  pinMode(PIN_ACC_INT, INPUT);

  w8(REG_CTRL1, 0x4F);  // ODR 50 Hz, low power mode, X/Y/Z enabled
  w8(REG_CTRL2, 0x09);  // high pass filter on INT1, removes the 1 g gravity bias
  w8(REG_CTRL3, 0x40);  // route IA1 to the INT1 pin
  w8(REG_CTRL4, 0x00);  // +/-2 g, no high resolution (low power)
  w8(REG_CTRL5, 0x08);  // latch INT1 until INT1_SRC is read
  setSensitivity(sensitivity);
  w8(REG_INT1_CFG, 0x2A);  // OR of X/Y/Z high events
  readAndClear();
  return true;
}

void setSensitivity(uint8_t sensitivity) {
  if (!present) return;
  w8(REG_INT1_THS, thresholdFor(sensitivity));
  w8(REG_INT1_DUR, 2);  // 2 samples at 50 Hz = 40 ms, filters single spikes
}

bool interruptActive() {
  if (!present) return false;
  return digitalRead(PIN_ACC_INT) == HIGH;
}

bool readAndClear() {
  if (!present) return false;
  return (r8(REG_INT1_SRC) & 0x40) != 0;  // IA bit
}

float magnitude() {
  if (!present) return NAN;
  Wire.beginTransmission(addr);
  Wire.write(REG_OUT_X_L | 0x80);  // auto increment
  if (Wire.endTransmission(false) != 0) return NAN;
  if (Wire.requestFrom(addr, static_cast<uint8_t>(6)) != 6) return NAN;

  int16_t raw[3];
  for (int i = 0; i < 3; i++) {
    uint8_t lo = Wire.read();
    uint8_t hi = Wire.read();
    raw[i] = static_cast<int16_t>((hi << 8) | lo);
  }
  // Low power mode is 8 bit left aligned: 16 mg per LSB of the high byte.
  const float g[3] = {(raw[0] >> 8) * 0.016f, (raw[1] >> 8) * 0.016f,
                      (raw[2] >> 8) * 0.016f};
  return sqrtf(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
}

bool sustainedMotion(uint32_t hold_ms) {
  if (!present) return false;
  const bool now = interruptActive() || readAndClear();
  const uint32_t t = millis();
  if (!now) {
    motion_since_ms = 0;
    return false;
  }
  if (motion_since_ms == 0) {
    motion_since_ms = t;
    return false;
  }
  return (t - motion_since_ms) >= hold_ms;
}

}  // namespace motion
