#include "hw.h"
#include "store.h"
#include <Wire.h>
#include <driver/rmt_tx.h>
#include <esp32-hal-rgb-led.h>

namespace hw {

static const uint8_t LED_PIN = 21;      // StampS3 onboard WS2812
static const uint8_t GROVE_SCL = 1;
static const uint8_t GROVE_SDA = 2;

// ---------------- RGB LED ----------------
static bool s_ledReady = false;

void ledBegin() { s_ledReady = true; ledOff(); }

void led(uint8_t r, uint8_t g, uint8_t b) {
    if (!s_ledReady) ledBegin();
    neopixelWrite(LED_PIN, r, g, b);     // in the core's HAL; no library needed
}

void ledOff() { neopixelWrite(LED_PIN, 0, 0, 0); }

void ledPulse(uint8_t r, uint8_t g, uint8_t b, uint16_t ms) {
    led(r, g, b);
    delay(ms);
    ledOff();
}

// ---------------- Grove I2C ----------------
// Addresses of M5 units and common breakouts, so a scan says something useful
// rather than just printing hex.
static const char* guessDevice(uint8_t addr) {
    switch (addr) {
        case 0x10: return "VEML6070 / light";
        case 0x18: return "SHT4x?";
        case 0x23: return "BH1750 light";
        case 0x29: return "VL53L0X distance / TCS34725";
        case 0x38: return "AHT20 / FT6336 touch";
        case 0x39: return "APDS-9960";
        case 0x3C: return "SSD1306 OLED";
        case 0x40: return "SHT30 / INA219 / HTU21";
        case 0x44: return "SHT30/31 temp+RH";
        case 0x48: return "ADS1115 / temp";
        case 0x51: return "BM8563 RTC";
        case 0x53: return "ADXL345";
        case 0x57: return "MAX30100 pulse";
        case 0x5A: return "MLX90614 / CCS811";
        case 0x62: return "SCD4x CO2";
        case 0x68: return "MPU6886 / DS3231";
        case 0x69: return "MPU6886 (alt)";
        case 0x70: return "TCA9548 mux";
        case 0x76: return "BMP280 / BME280";
        case 0x77: return "BMP280 (alt)";
        default:   return "unknown";
    }
}

std::vector<I2CDevice> i2cScan() {
    std::vector<I2CDevice> found;
    Wire.end();
    Wire.begin(GROVE_SDA, GROVE_SCL, 100000);
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) found.push_back({addr, guessDevice(addr)});
        delay(2);
    }
    Wire.end();
    return found;
}

// ---------------- Infrared ----------------
const char* irProtoName(IrProto p) {
    switch (p) {
        case IrProto::NEC:        return "NEC";
        case IrProto::Samsung:    return "Samsung";
        case IrProto::SonySIRC12: return "Sony 12";
        case IrProto::SonySIRC20: return "Sony 20";
        case IrProto::RC5:        return "RC5";
        default: return "?";
    }
}

uint8_t irPin() { return (uint8_t)store::getInt("irpin", 44); }
void setIrPin(uint8_t p) { store::setInt("irpin", p); }

// Bit-banged carrier. A hardware timer would jitter less, but IR receivers
// demodulate with a wide tolerance and this keeps the driver dependency-free.
static void mark(uint32_t us, uint16_t carrierHz) {
    uint8_t pin = irPin();
    uint32_t halfPeriod = 500000UL / carrierHz;      // microseconds, half a cycle
    uint32_t end = micros() + us;
    while ((int32_t)(end - micros()) > 0) {
        digitalWrite(pin, HIGH);
        delayMicroseconds(halfPeriod);
        digitalWrite(pin, LOW);
        delayMicroseconds(halfPeriod);
    }
}

static void space(uint32_t us) {
    digitalWrite(irPin(), LOW);
    uint32_t end = micros() + us;
    while ((int32_t)(end - micros()) > 0) {}
}

static void sendNecLike(uint32_t data, int bits, uint32_t hdrMark, uint32_t hdrSpace,
                        uint32_t bitMark, uint32_t oneSpace, uint32_t zeroSpace) {
    mark(hdrMark, 38000);
    space(hdrSpace);
    for (int i = bits - 1; i >= 0; i--) {
        mark(bitMark, 38000);
        space((data >> i) & 1 ? oneSpace : zeroSpace);
    }
    mark(bitMark, 38000);
    space(0);
}

static void sendSony(uint32_t data, int bits) {
    // Sony wants the frame repeated at least three times to be accepted.
    for (int rep = 0; rep < 3; rep++) {
        mark(2400, 40000);
        space(600);
        for (int i = 0; i < bits; i++) {           // Sony is LSB-first
            if ((data >> i) & 1) mark(1200, 40000); else mark(600, 40000);
            space(600);
        }
        delay(24);
    }
}

static void sendRc5(uint32_t data, int bits) {
    // Manchester: 1 = space then mark, 0 = mark then space.
    const uint32_t half = 889;
    for (int i = bits - 1; i >= 0; i--) {
        if ((data >> i) & 1) { space(half); mark(half, 36000); }
        else                 { mark(half, 36000); space(half); }
    }
    space(0);
}

void irSend(IrProto proto, uint32_t address, uint32_t command) {
    uint8_t pin = irPin();
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);

    noInterrupts();                 // timing here is the whole protocol
    switch (proto) {
        case IrProto::NEC: {
            uint8_t a = address & 0xFF, c = command & 0xFF;
            uint32_t frame = ((uint32_t)a << 24) | ((uint32_t)(uint8_t)~a << 16) |
                             ((uint32_t)c << 8) | (uint8_t)~c;
            sendNecLike(frame, 32, 9000, 4500, 560, 1690, 560);
            break;
        }
        case IrProto::Samsung: {
            uint8_t a = address & 0xFF, c = command & 0xFF;
            uint32_t frame = ((uint32_t)a << 24) | ((uint32_t)a << 16) |
                             ((uint32_t)c << 8) | (uint8_t)~c;
            sendNecLike(frame, 32, 4500, 4500, 560, 1690, 560);
            break;
        }
        case IrProto::SonySIRC12:
            sendSony(((address & 0x1F) << 7) | (command & 0x7F), 12);
            break;
        case IrProto::SonySIRC20:
            sendSony(((address & 0x1FFF) << 7) | (command & 0x7F), 20);
            break;
        case IrProto::RC5: {
            uint32_t frame = (1 << 13) | (1 << 12) |          // start bits
                             ((address & 0x1F) << 6) | (command & 0x3F);
            sendRc5(frame, 14);
            break;
        }
        default: break;
    }
    interrupts();
    digitalWrite(pin, LOW);
}

}  // namespace hw
