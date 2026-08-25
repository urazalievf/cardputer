#include "hw.h"
#include <algorithm>
#include "store.h"
#include <Wire.h>
#include <esp32-hal-rgb-led.h>

// rgbLedWrite() is the current name; neopixelWrite() is deprecated in IDF 5.5.
#ifndef rgbLedWrite
#define CARDPUTER_RGB_WRITE rgbLedWrite
#else
#define CARDPUTER_RGB_WRITE rgbLedWrite
#endif

namespace hw {

// ---------------- battery ----------------
static Battery  s_batt;
static int      s_ring[9] = {0};
static int      s_ringFill = 0, s_ringHead = 0;
static float    s_ema = -1.0f;
static int      s_shown = -1;
static uint32_t s_lastSample = 0;
static int      s_markPct = -1;
static uint32_t s_markMs = 0;

const Battery& battery() { return s_batt; }

void batteryTick() {
    uint32_t now = millis();
    // One sample per 250ms, spread across ticks rather than taken back to back:
    // seven reads in a tight loop measure the same instant of noise and the
    // median of them is no better than one of them.
    if (s_lastSample && now - s_lastSample < 250) return;
    s_lastSample = now ? now : 1;

    int raw = M5Cardputer.Power.getBatteryLevel();
    if (raw < 0) { s_batt.known = false; return; }
    s_batt.raw = raw;

    s_ring[s_ringHead] = raw;
    s_ringHead = (s_ringHead + 1) % 9;
    if (s_ringFill < 9) s_ringFill++;

    int sorted[9];
    for (int i = 0; i < s_ringFill; i++) sorted[i] = s_ring[i];
    std::sort(sorted, sorted + s_ringFill);
    int med = sorted[s_ringFill / 2];

    if (s_ringFill < 3) {
        // Converge straight to the first readings rather than ramping: starting
        // the average at zero would show a flat battery for the first seconds of
        // every boot, and rate-limiting from a bad first sample would take ten
        // seconds to walk up to the truth.
        s_ema = med;
        s_shown = med;
    } else {
        s_ema += (med - s_ema) * 0.15f;
        // From here the display moves at most one point per sample, and only
        // once the filtered value has clearly crossed -- the dead band is what
        // stops it flickering between two neighbouring numbers.
        if      (s_ema > s_shown + 0.75f) s_shown++;
        else if (s_ema < s_shown - 0.75f) s_shown--;
    }
    s_shown = s_shown < 0 ? 0 : s_shown > 100 ? 100 : s_shown;

    s_batt.known = true;
    s_batt.percent = s_shown;

    // No charge-status line on this board, so infer it from the one thing that
    // is only true of a charging cell: the level rises. Judged over a minute,
    // because a single point of drift is not evidence of anything.
    if (s_markPct < 0) { s_markPct = s_shown; s_markMs = now; }
    else if (now - s_markMs > 60000) {
        if      (s_shown > s_markPct) s_batt.charging = true;
        else if (s_shown < s_markPct) s_batt.charging = false;
        // Equal: a full cell on a charger holds steady, so keep the verdict.
        s_markPct = s_shown;
        s_markMs = now;
    }
}


static const uint8_t LED_PIN = 21;      // StampS3 onboard WS2812
static const uint8_t GROVE_SCL = 1;
static const uint8_t GROVE_SDA = 2;

// ---------------- RGB LED ----------------
static bool s_ledReady = false;

void ledBegin() { s_ledReady = true; ledOff(); }

void led(uint8_t r, uint8_t g, uint8_t b) {
    if (!s_ledReady) ledBegin();
    CARDPUTER_RGB_WRITE(LED_PIN, r, g, b);   // in the core's HAL; no library needed
}

void ledOff() { CARDPUTER_RGB_WRITE(LED_PIN, 0, 0, 0); }

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
// demodulate with wide tolerance and this keeps the driver dependency-free.
//
// Interrupts are masked only for the duration of one mark or space -- never
// across a whole frame. Masking for ~70ms would starve WiFi and trip the task
// watchdog, and any delay() taken while masked never returns at all, because
// FreeRTOS needs the tick interrupt to resume the caller.
static portMUX_TYPE s_irMux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t s_irPinCached = 44;

static void mark(uint32_t us, uint16_t carrierHz) {
    const uint32_t halfPeriod = 500000UL / carrierHz;   // half a carrier cycle, us
    portENTER_CRITICAL(&s_irMux);
    uint32_t start = micros();
    while (micros() - start < us) {
        digitalWrite(s_irPinCached, HIGH);
        delayMicroseconds(halfPeriod);
        digitalWrite(s_irPinCached, LOW);
        delayMicroseconds(halfPeriod);
    }
    portEXIT_CRITICAL(&s_irMux);
}

static void space(uint32_t us) {
    digitalWrite(s_irPinCached, LOW);
    if (us == 0) return;
    // Long gaps yield instead of spinning, so the scheduler still runs.
    if (us > 4000) { delayMicroseconds(2000); delay((us - 2000) / 1000); return; }
    portENTER_CRITICAL(&s_irMux);
    uint32_t start = micros();
    while (micros() - start < us) {}
    portEXIT_CRITICAL(&s_irMux);
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

static void sendSonyFrame(uint32_t data, int bits) {
    mark(2400, 40000);
    space(600);
    for (int i = 0; i < bits; i++) {               // Sony is LSB-first
        if ((data >> i) & 1) mark(1200, 40000); else mark(600, 40000);
        space(600);
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
    s_irPinCached = irPin();        // read once: NVS must not be touched mid-frame
    pinMode(s_irPinCached, OUTPUT);
    digitalWrite(s_irPinCached, LOW);

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
        case IrProto::SonySIRC20: {
            // Sony receivers want the frame at least three times, 45ms apart.
            // The gap is a real delay() -- outside any critical section.
            bool wide = proto == IrProto::SonySIRC20;
            uint32_t frame = wide ? (((address & 0x1FFF) << 7) | (command & 0x7F))
                                  : (((address & 0x1F) << 7) | (command & 0x7F));
            for (int rep = 0; rep < 3; rep++) {
                sendSonyFrame(frame, wide ? 20 : 12);
                delay(24);
            }
            break;
        }
        case IrProto::RC5: {
            uint32_t frame = (1 << 13) | (1 << 12) |          // start bits
                             ((address & 0x1F) << 6) | (command & 0x3F);
            sendRc5(frame, 14);
            break;
        }
        default: break;
    }
    digitalWrite(s_irPinCached, LOW);
}

}  // namespace hw
