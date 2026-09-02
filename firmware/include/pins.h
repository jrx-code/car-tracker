// Pin map. One header, one block per hardware variant.
// Keep every GPIO number here, never inline in the code.
#pragma once

#if defined(BOARD_LILYGO_TA7670)

// LilyGO T-A7670G. Values taken from the vendor board definition.
// [TO VERIFY on the actual board before the first power-up: LilyGO changes
//  the pin map between board revisions, check the silkscreen and the wiki.]
#define PIN_MODEM_TX 26
#define PIN_MODEM_RX 27
#define PIN_MODEM_PWRKEY 4
#define PIN_MODEM_POWER_EN 12  // board power latch, not a load switch
#define PIN_MODEM_RESET 5

#define PIN_GNSS_TX 21
#define PIN_GNSS_RX 22
#define PIN_GNSS_EN -1  // no separate GNSS rail on this board

#define PIN_VBAT_ADC 35
#define PIN_ACC_SDA 15
#define PIN_ACC_SCL 14
#define PIN_ACC_INT 13
#define PIN_LED 12

#else

// Discrete build: bare ESP32 module + modem breakout + NEO-6M.
// Load switches are active high P-MOSFET drivers, see docs/04 section 4.3.
#define PIN_MODEM_TX 17
#define PIN_MODEM_RX 16
#define PIN_MODEM_PWRKEY 4
#define PIN_MODEM_POWER_EN 25  // load switch: cuts the 3.8 V modem rail
#define PIN_MODEM_RESET -1

#define PIN_GNSS_TX 27  // ESP32 TX -> NEO-6M RX
#define PIN_GNSS_RX 26  // ESP32 RX <- NEO-6M TX
#define PIN_GNSS_EN 33  // load switch: cuts the 3.3 V GNSS rail

#define PIN_VBAT_ADC 34  // divider 560k/100k from OBD pin 16, see docs/04 section 4.4
#define PIN_ACC_SDA 21
#define PIN_ACC_SCL 22
#define PIN_ACC_INT 35  // RTC-capable GPIO, required for ext0 deep sleep wake
#define PIN_LED 2

// Phase 2 only, left unconnected in v1. See docs/06-can-obd.md.
// GPIO 5 is a strapping pin and must be high at boot; a CAN transceiver idles
// its TX input high, so this works, but verify the board still boots with the
// transceiver attached before trusting it.
#define PIN_CAN_TX 5
#define PIN_CAN_RX 18
// Load switch for the transceiver rail. Not a standby pin: with the engine off
// the transceiver must be electrically absent from the bus, and the only way to
// be sure of that is no supply at all (docs/06 section 6.4).
#define PIN_CAN_EN 32

#endif

#define GNSS_BAUD 9600
#define MODEM_BAUD 115200
