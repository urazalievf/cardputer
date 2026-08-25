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

**There is no PMIC, so the battery level is a bare ADC read.** `getBatteryLevel()`
samples the cell once and scales it `(mV - 3300) * 100 / 800` — one percentage
point is eight millivolts. WiFi transmit bursts and a charger's switching
regulator move that rail by tens of millivolts, so an unfiltered reading swings
ten points while sitting still, worst of all while plugged in. It was also
recomputed on every status-bar repaint.

`hw::batteryTick()` filters it in three stages: one sample per 250 ms into a
nine-deep ring, the median of that ring (spikes do not survive a median), an
exponential average over the medians, and finally a displayed value that moves
at most one point per sample and only once the average has crossed a dead band.
The first three samples seed it directly so a boot does not ramp up from zero.

`isCharging()` has no branch for this board and returns `charge_unknown`, so
charging is inferred from the only thing that is only ever true of a charging
cell: the level goes up. Judged over a minute, because one point of drift is not
evidence.

**The microphone and the speaker fight over GPIO43.** Checked against
M5Unified 0.2.7, `board_M5Cardputer` wires them like this:

| Path | Pins |
|---|---|
| Microphone (PDM, I2S_NUM_0) | data `GPIO46`, clock `GPIO43`, no BCK |
| Speaker (I2S_NUM_1) | BCK `GPIO41`, WS `GPIO43`, DOUT `GPIO42` |
| SD (SPI) | SCK `GPIO40`, MISO `GPIO39`, MOSI `GPIO14`, CS `GPIO12` |

`GPIO43` is the overlap, and it is between the two audio paths only — which is
why `audio::micOn()` and `speakerOn()` each end the other, and why exactly one
of them can be live.

An earlier version of this file claimed the SD clock and the I2S bit clock were
both `GPIO40`. That is not true of this library version: the mic runs in PDM
mode with no BCK at all, and the speaker's BCK is `GPIO41`. The SD bus shares no
pin with either.

The arbitration between them is still in place — `store::sdAcquire()` calls
`audio::releaseI2S()`, and `audio::micOn()` calls `store::sdRelease()`. It is
kept deliberately rather than removed on the strength of a pinout read: the
card works today, the commit that added it was fixing a real mount failure, and
nobody has re-tested a build without it. It costs an unnecessary unmount on
every recording, which is worth knowing before anyone tries to save a file
while the microphone is live. `Voice` writes its WAV after the capture ends,
for exactly this reason.

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

**Formatting a large card takes a long time, and looks like a hang.** The
Arduino SD library gives `f_mkfs` a work buffer of `FF_MAX_SS` -- 4KB with this
sdkconfig -- so zeroing the FAT tables on a 256GB card takes tens of seconds
with no progress reporting. Settings > Format SD card runs it behind
`ui::await()` so the elapsed counter keeps moving; without that it is
indistinguishable from a crash, and the natural reaction is to pull the plug
half way through.

**Mounting the card costs ~29KB of heap** in driver, FATFS and VFS structures.
That matters because it comes straight off the maximum recording length. Since
claiming the microphone unmounts the card anyway (see the arbitration above),
`audio::recordStart()` releases it *before* allocating the capture buffer, and
`store` tells `audio` how much a mounted card is holding so the advertised
capacity reflects what recording will actually get.

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
