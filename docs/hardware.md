# Hardware

**M5Stack Cardputer v1.1** — the StampS3 version.

| | |
|---|---|
| SoC | ESP32-S3FN8 (StampS3), dual-core @ 240 MHz |
| Flash | 8 MB |
| PSRAM | none on stock StampS3 |
| Display | 1.14" ST7789, 240×135, landscape at rotation 1 |
| Keyboard | 56 keys, I2C matrix |
| Mic | SPM1423 PDM |
| Speaker | NS4168 amp — **shares I2S with the mic** |
| SD | SPI: SCK 40, MISO 39, MOSI 14, CS 12 |
| Battery | 120 mAh internal, 1400 mAh in the base |
| Bluetooth | BLE only — no Classic, no A2DP, no SPP |
| RGB LED | WS2812 on GPIO21 (on the StampS3 itself) |
| Grove Port A | I2C — SCL on G1, SDA on G2 |
| Infrared | transmit only, G44 by default (configurable) |
| Extras | Grove port, IR, USB-C |

## Things that bite

**The SD card and the audio path fight over GPIO40.** This is the big one, and
it is not obvious from any pinout diagram. `M5Unified.cpp` lists the Cardputer's
SD clock as GPIO40 — and the same file sets `mic_cfg.pin_bck = GPIO_NUM_40` and
`spk_cfg.pin_bck = GPIO_NUM_40`. One pin, two peripherals. Because
`M5Cardputer.begin()` starts the speaker, I2S owns GPIO40 before any SD mount
runs, and the card silently fails to appear.

`store::sdAcquire()` and `audio::releaseI2S()` arbitrate: every SD entry point
claims the pin (evicting audio), and `audio::micOn()` / `speakerOn()` call
`store::sdRelease()` first. The idle state favours the card, since most apps
read files and few record. `store::sdReady()` reports whether a card was ever
seen, not whether it is mounted right now, so the status bar doesn't flicker
while the mic has the pin.

**Mic and speaker cannot both be live either.** Same I2S peripheral.
`audio::micOn()` and `audio::speakerOn()` call `end()` on the other and wait
~60 ms for the driver to release. Every beep during recording costs that round
trip.

**The card must be FAT32.** The Arduino SD library mounts FAT16/FAT32 only.
Cards 64GB and larger ship exFAT from the factory and fail with
`f_mount failed: (13) There is no valid FAT volume` — which looks identical to
"no card" unless `CORE_DEBUG_LEVEL` is at least 1. It is set to 1 in
`platformio.ini` for exactly this reason.

**No PSRAM means no big audio buffer.** `audio::begin()` asks for 30 s of
16 kHz mono, gets nothing from SPIRAM on a stock unit, and falls back to half
the free internal heap capped at 160 KB — about 5 seconds. Leave the rest:
TLS handshakes need ~40 KB of contiguous heap, and a full buffer plus a
`WiFiClientSecure` will OOM.

**Arrow keys are `fn` + `;` `.` `,` `/`.** The glyphs are printed on the
keycaps. `os::translate()` maps them; apps see `k.up`/`k.down`/`k.left`/`k.right`.

**The `` ` `` key is Esc.** The kernel intercepts it for back/home unless an app
returns `false` from `escExits()`. Text fields drop it rather than inserting a
backtick.

**Bluetooth needs the right toolchain.** ESP32-S3 has BLE but no Classic BT, so
there is no A2DP or SPP — a BLE HID keyboard and a scanner are what the radio
can actually do. More subtly, this project originally pinned
`framework-arduinoespressif32-libs` to the Bruce-firmware build, whose
`sdkconfig` says `# CONFIG_BT_ENABLED is not set`: Bluetooth is compiled out and
there is no `libbt.a` to link against. Every BLE symbol fails to resolve with a
confusing "does not name a type", because the headers are present but their
bodies are behind `#if CONFIG_BT_ENABLED`. The pin is gone; the platform's stock
libs ship BT enabled and cost ~290KB of flash.

**Radio coexistence.** BLE and WiFi share one antenna. BLE runs at
`ESP_PWR_LVL_P3` and only while the Bluetooth app is open, which keeps both
usable and reclaims ~60KB of heap the rest of the time.

**Never mask interrupts across an IR frame.** This one hangs the board outright.
A 32-bit NEC frame takes ~70ms; holding `noInterrupts()` for that long starves
WiFi and trips the task watchdog. Worse, Sony wants its frame repeated three
times ~24ms apart, and `delay()` inside a masked window *never returns* — it
yields to FreeRTOS, which needs the tick interrupt to resume the caller. The
driver now masks only for the duration of a single mark or space, spins on
`micros()` for short gaps, and yields properly for long ones.

**The RGB LED is on the StampS3, not the Cardputer board.** GPIO21, a single
WS2812, driven by the core's own `rgbLedWrite()` — no library needed. It is
worth using: it is the only output visible when the screen is face-down, which
is exactly the situation while recording a voice memo.

**USB mass storage does not work yet.** The goal was to mount the SD card in
Finder over the same cable that powers the device. `kernel/usbdisk.cpp` is
written and compiles, and `env:cardputer-usbdrive` builds it, but no volume ever
appears. What is established:

- Mass storage requires TinyUSB (`ARDUINO_USB_MODE=0`). The hardware CDC/JTAG
  bridge cannot present an MSC interface at all.
- With `ARDUINO_USB_CDC_ON_BOOT=1`, the core calls `USB.begin()` from `main()`
  *before* `setup()` runs (`cores/esp32/main.cpp:109`), which freezes the
  descriptor. Registering MSC in `setup()` is silently too late.
  `ARDUINO_USB_ON_BOOT` is a plain `#define` in `USB.h`, not `#ifndef`-guarded,
  so it cannot be overridden from `build_flags`.
- Working around that with `ARDUINO_USB_CDC_ON_BOOT=0` plus an explicit
  `USB.begin()` after registering MSC does produce a composite device
  (`bDeviceClass 239`, "M5Stack StampS3"), but still no volume — and that
  build's CDC console is silent, so there is nothing to debug from.
- In TinyUSB mode esptool cannot reset the board: uploads fail with "No serial
  data received". A 1200-baud touch reliably drops it back to the ROM
  bootloader; `tools/usb_touch.py` automates this as a pre-upload action.

Until this is solved, the Share app serves the card over HTTP instead, which
needs no USB gymnastics and works on any device with a browser.

**Flashing.** Usually just works over USB CDC. If the port doesn't appear, hold
the G0 button on the StampS3 while plugging in to force download mode.
