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
