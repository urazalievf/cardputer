#include "usbdisk.h"
#include "store.h"
#include "audio.h"
#include <SPI.h>

#if ARDUINO_USB_MODE == 0
#include <USB.h>
#include <USBMSC.h>
#include <sd_diskio.h>
#endif

namespace usbdisk {

#if ARDUINO_USB_MODE == 0

// Cardputer SD slot (SCK 40, MISO 39, MOSI 14, CS 12). Shares no pin with the
// audio path — see docs/hardware.md.
static const int SD_SCK = 40, SD_MISO = 39, SD_MOSI = 14, SD_CS = 12;

static USBMSC   s_msc;
static bool     s_available = false;
static bool     s_attached = false;
static uint8_t  s_pdrv = 0xFF;
static uint32_t s_sectors = 0;
static uint16_t s_sectorSize = 512;
static volatile uint32_t s_reads = 0, s_writes = 0;

static int32_t onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
    if (s_pdrv == 0xFF) return -1;
    // The host may ask for a run of sectors; sd_read_raw does one at a time.
    uint32_t count = bufsize / s_sectorSize;
    uint8_t* out = (uint8_t*)buffer;
    for (uint32_t i = 0; i < count; i++) {
        if (!sd_read_raw(s_pdrv, out + i * s_sectorSize, lba + i)) return -1;
    }
    s_reads += count;
    return count * s_sectorSize;
}

static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
    if (s_pdrv == 0xFF) return -1;
    uint32_t count = bufsize / s_sectorSize;
    for (uint32_t i = 0; i < count; i++) {
        if (!sd_write_raw(s_pdrv, buffer + i * s_sectorSize, lba + i)) return -1;
    }
    s_writes += count;
    return count * s_sectorSize;
}

// The host ejecting the volume should hand the card back, not leave it in limbo.
static bool onStartStop(uint8_t power_condition, bool start, bool load_eject) {
    if (load_eject && !start) {
        os::logf("usbdisk: host ejected");
        detach();
    }
    return true;
}

void begin() {
    // Probe once for geometry, then let go: Arduino's SD needs the bus back for
    // normal operation, and MSC only claims it when the mode is switched on.
    // Nothing else is initialised yet in the USB-drive build, so this must not
    // reach into audio or store.
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    uint8_t pdrv = sdcard_init(SD_CS, &SPI, 20000000);
    if (pdrv == 0xFF) {
        os::logf("usbdisk: no card at boot - USB drive unavailable this session");
        SPI.end();
        return;
    }
    // sdcard_init() allocates a driver slot; it does not address the card. Until
    // something runs ff_sd_initialize() the card is still in idle, the geometry
    // reads back as zero sectors, and every raw read fails -- so this registered
    // a zero-sized volume and the host, quite reasonably, showed nothing.
    if (!store::sdRawInit(pdrv)) {
        os::logf("usbdisk: card would not initialise - USB drive unavailable");
        sdcard_uninit(pdrv);
        SPI.end();
        return;
    }
    s_sectors = sdcard_num_sectors(pdrv);
    s_sectorSize = sdcard_sector_size(pdrv);
    sdcard_uninit(pdrv);
    SPI.end();

    if (!s_sectors || !s_sectorSize) {
        os::logf("usbdisk: card reported no geometry");
        return;
    }

    s_msc.vendorID("M5Stack");
    s_msc.productID("CardputerOS SD");
    s_msc.productRevision("1.0");
    s_msc.onRead(onRead);
    s_msc.onWrite(onWrite);
    s_msc.onStartStop(onStartStop);
    s_msc.mediaPresent(false);            // registered, but reported empty
    s_msc.isWritable(true);
    s_msc.begin(s_sectors, s_sectorSize);
    s_available = true;
    os::logf("usbdisk: ready, %u sectors x %u B (%llu MB), media reported absent",
             (unsigned)s_sectors, (unsigned)s_sectorSize, sizeMB());
}

bool attach() {
    if (!s_available || s_attached) return s_attached;
    // Both sides cannot own the card. Drop our filesystem view entirely.
    audio::releaseI2S();
    store::sdRelease();
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    s_pdrv = sdcard_init(SD_CS, &SPI, 20000000);
    if (s_pdrv == 0xFF) {
        os::logf("usbdisk: could not claim the card");
        SPI.end();
        return false;
    }
    // Same again: a slot is not a live card, and MSC serves raw sectors.
    if (!store::sdRawInit(s_pdrv)) {
        os::logf("usbdisk: card would not initialise on attach");
        sdcard_uninit(s_pdrv);
        s_pdrv = 0xFF;
        SPI.end();
        return false;
    }
    s_reads = s_writes = 0;
    store::setUsbOwned(true);
    s_msc.mediaPresent(true);
    s_attached = true;
    os::logf("usbdisk: attached to host");
    return true;
}

void detach() {
    if (!s_attached) return;
    s_msc.mediaPresent(false);
    if (s_pdrv != 0xFF) { sdcard_uninit(s_pdrv); s_pdrv = 0xFF; }
    SPI.end();
    s_attached = false;
    store::setUsbOwned(false);
    os::logf("usbdisk: detached, %u sectors read / %u written",
             (unsigned)s_reads, (unsigned)s_writes);
}

bool available() { return s_available; }
bool attached() { return s_attached; }
uint32_t sectorCount() { return s_sectors; }
uint64_t sizeMB() { return (uint64_t)s_sectors * s_sectorSize / (1024ULL * 1024ULL); }
uint32_t readCount() { return s_reads; }
uint32_t writeCount() { return s_writes; }

#else   // ARDUINO_USB_MODE == 1: hardware CDC/JTAG, no TinyUSB, no MSC

void begin() { os::logf("usbdisk: needs ARDUINO_USB_MODE=0 (TinyUSB)"); }
bool available() { return false; }
bool attached() { return false; }
uint32_t sectorCount() { return 0; }
uint64_t sizeMB() { return 0; }
uint32_t readCount() { return 0; }
uint32_t writeCount() { return 0; }
bool attach() { return false; }
void detach() {}

#endif

}  // namespace usbdisk
