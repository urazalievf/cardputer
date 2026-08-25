// Hardware the rest of the OS wasn't using: the StampS3's RGB LED, the IR
// transmitter, and the Grove port's I2C bus.
#pragma once
#include "os.h"
#include <vector>

namespace hw {

// --- RGB LED (WS2812 on GPIO21) ---
void ledBegin();
void led(uint8_t r, uint8_t g, uint8_t b);
void ledOff();
void ledPulse(uint8_t r, uint8_t g, uint8_t b, uint16_t ms);

// --- Battery ---
// The Cardputer has no PMIC. getBatteryLevel() is a bare ADC read of the cell
// scaled (mV - 3300) * 100 / 800, so one percentage point is eight millivolts,
// and it is recomputed from a single sample on every call. WiFi transmit bursts
// and a charger's switching regulator move that rail by tens of millivolts, so
// an unfiltered reading swings ten points at a stand -- worst of all while
// plugged in, which is exactly when people watch it.
//
// This filters in three stages: a median over the last couple of seconds to
// throw out spikes, an exponential average to smooth what is left, and a
// rate-limited display value so the number never moves more than a point at a
// time. Call batteryTick() once per event-loop pass; it samples on its own
// schedule and costs one ADC read every 250ms.
struct Battery {
    bool known    = false;
    int  percent  = 0;       // filtered, 0-100
    bool charging = false;   // inferred; this board has no charge-status line
    int  raw      = 0;       // last unfiltered sample, for diagnostics
};
const Battery& battery();
void batteryTick();

// --- Grove port I2C (G1 = SCL, G2 = SDA) ---
struct I2CDevice { uint8_t addr; const char* guess; };
std::vector<I2CDevice> i2cScan();

// --- Infrared transmitter ---
// The Cardputer's IR LED is on G44 by default; it is a setting because a
// wrong pin fails silently and there is no receiver to check against.
enum class IrProto : uint8_t { NEC, Samsung, SonySIRC12, SonySIRC20, RC5, COUNT };
const char* irProtoName(IrProto p);
uint8_t irPin();
void setIrPin(uint8_t p);
void irSend(IrProto proto, uint32_t address, uint32_t command);

}  // namespace hw
