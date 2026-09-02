#include "power.h"

#include <Preferences.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <esp_system.h>

#include "pins.h"

namespace power {
namespace {

Preferences prefs;
bool modem_on = false;
bool gnss_on = false;

// Calibration: vbat = raw * gain + offset. Defaults assume a 470k/100k divider
// and the nominal ADC scale; they are only a starting point and are expected to
// be replaced by setCalibration() during step W4 of docs/11-plan-wdrozenia.md.
float cal_gain = (3.3f / 4095.0f) * ((470.0f + 100.0f) / 100.0f);
float cal_offset = 0.0f;

constexpr int kAdcSamples = 32;

}  // namespace

void begin() {
  analogReadResolution(12);
  // 11 dB attenuation gives the widest input range on the ESP32 ADC.
  analogSetPinAttenuation(PIN_VBAT_ADC, ADC_11db);

  if (PIN_MODEM_POWER_EN >= 0) {
    pinMode(PIN_MODEM_POWER_EN, OUTPUT);
    digitalWrite(PIN_MODEM_POWER_EN, LOW);
  }
  if (PIN_GNSS_EN >= 0) {
    pinMode(PIN_GNSS_EN, OUTPUT);
    digitalWrite(PIN_GNSS_EN, LOW);
  }
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  prefs.begin("power", true);
  cal_gain = prefs.getFloat("gain", cal_gain);
  cal_offset = prefs.getFloat("offset", cal_offset);
  prefs.end();
}

float rawAdcAvg() {
  uint32_t sum = 0;
  for (int i = 0; i < kAdcSamples; i++) {
    sum += analogRead(PIN_VBAT_ADC);
    delayMicroseconds(200);
  }
  return static_cast<float>(sum) / kAdcSamples;
}

float readVbat() { return rawAdcAvg() * cal_gain + cal_offset; }

float readVsys() {
  // ESP32 has no dedicated supply sense pin; the internal reference reading is
  // coarse. Reported as diagnostics only, never used for a decision.
  return 3.3f;
}

void setCalibration(float raw_lo, float v_lo, float raw_hi, float v_hi) {
  if (raw_hi == raw_lo) return;
  cal_gain = (v_hi - v_lo) / (raw_hi - raw_lo);
  cal_offset = v_lo - cal_gain * raw_lo;
  prefs.begin("power", false);
  prefs.putFloat("gain", cal_gain);
  prefs.putFloat("offset", cal_offset);
  prefs.end();
}

void modemPower(bool on) {
  if (PIN_MODEM_POWER_EN < 0) return;
  digitalWrite(PIN_MODEM_POWER_EN, on ? HIGH : LOW);
  modem_on = on;
  if (on) delay(50);  // let the 3.8 V rail settle before touching PWRKEY
}

void gnssPower(bool on) {
  if (PIN_GNSS_EN < 0) return;
  digitalWrite(PIN_GNSS_EN, on ? HIGH : LOW);
  gnss_on = on;
}

bool modemPowered() { return modem_on; }
bool gnssPowered() { return gnss_on; }

void deepSleep(uint32_t seconds, bool wake_on_motion) {
  // Cut both rails first. Sleeping the peripherals instead of cutting them is
  // the single most common way to blow the current budget in docs/04.
  modemPower(false);
  gnssPower(false);
  digitalWrite(PIN_LED, LOW);

  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(seconds) * 1000000ULL);

  if (wake_on_motion && PIN_ACC_INT >= 0) {
    // LIS3DH INT1 is configured active high, so wake on level 1.
    rtc_gpio_pullup_dis(static_cast<gpio_num_t>(PIN_ACC_INT));
    rtc_gpio_pulldown_en(static_cast<gpio_num_t>(PIN_ACC_INT));
    esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(PIN_ACC_INT), 1);
  }

  Serial.flush();
  esp_deep_sleep_start();
}

uint8_t resetReason() { return static_cast<uint8_t>(esp_reset_reason()); }

}  // namespace power
