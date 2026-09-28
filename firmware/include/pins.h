// Pin map. One header, one block per hardware variant.
// Keep every GPIO number here, never inline in the code.
#pragma once

#if defined(BOARD_LILYGO_TA7670)

// LilyGO T-A7670E R2 (A7670E-FASE). Values from the vendor board definition,
// LilyGO-T-A76XX examples/ATdebug/utilities.h, block LILYGO_T_A7670, and
// checked against the board on the bench on 2026-09-28.
#define PIN_MODEM_TX 26
#define PIN_MODEM_RX 27
#define PIN_MODEM_PWRKEY 4
#define PIN_MODEM_POWER_EN 12  // BOARD_POWERON: must be HIGH or the modem is unpowered
#define PIN_MODEM_RESET 5      // active HIGH on this board
#define PIN_MODEM_DTR 25
#define PIN_MODEM_RING 33

// No GNSS UART on the ESP32 side: the receiver lives in the A7670E-FASE and
// is read over AT (MODEM_HAS_GNSS). -1 disables the UART path in gnss.cpp.
#define PIN_GNSS_TX -1
#define PIN_GNSS_RX -1
#define PIN_GNSS_EN -1

// GPIO 35 on this board is the Li-ion cell divider (BOARD_BAT_ADC), not the
// OBD supply. Read through the OBD calibration it showed 12.85 V on USB power
// and sent the bench board into deep sleep. Car voltage needs its own divider
// on a free ADC pin; until then -1, which is the "no divider" bench path.
#define PIN_VBAT_ADC -1
// 21/22 are the board I2C pins. 13/14/15 (and 2) belong to the on-board
// micro SD slot, so the accelerometer must not go there. The LIS3DH is an
// external part; INT goes to 32, an RTC GPIO, so ext0 deep sleep wake works.
#define PIN_ACC_SDA 21
#define PIN_ACC_SCL 22
#define PIN_ACC_INT 32
#define PIN_LED -1  // no user LED; GPIO 12 is the modem power latch

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
// GPIO 13 and not 14: 14 outputs a PWM signal at boot, so it would fight the
// transceiver's push-pull R output on every reset. 13 is neither a strapping
// pin nor active at boot; its only other role is JTAG MTCK, which matters only
// with a hardware debugger attached. Note the LilyGO block above uses 13 for
// PIN_ACC_INT, so the two variants cannot share a wiring harness here.
#define PIN_CAN_RX 13
// Load switch for the transceiver rail. Not a standby pin: with the engine off
// the transceiver must be electrically absent from the bus, and the only way to
// be sure of that is no supply at all (docs/06 section 6.4).
#define PIN_CAN_EN 32

#endif

#define GNSS_BAUD 9600
#define MODEM_BAUD 115200
