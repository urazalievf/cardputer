#include "usbdisk.h"
#include "store.h"
#include "audio.h"
#include <SPI.h>
#include <atomic>

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

// Mass storage is throughput-bound in a way normal file access is not: a host
// mounting FAT32 reads the whole FAT before it will show you a single file.
// Try fast, fall back for cards that will not take it.
static uint8_t initCard(void) {
    for (int hz : {40000000, 25000000, 20000000}) {
        uint8_t pdrv = sdcard_init(SD_CS, &SPI, hz);
        if (pdrv == 0xFF) continue;
        if (store::sdRawInit(pdrv)) {
            os::logf("usbdisk: card initialised at %d MHz", hz / 1000000);
            return pdrv;
        }
        sdcard_uninit(pdrv);
    }
    return 0xFF;
}

static USBMSC   s_msc;
static bool     s_available = false;
static bool     s_attached = false;
static uint8_t  s_pdrv = 0xFF;
static uint32_t s_sectors = 0;
static uint16_t s_sectorSize = 512;
// Written from TinyUSB's task and read from the event loop, so atomic rather
// than volatile -- which does not order anything and whose ++ is deprecated.
// Relaxed is right: these are counters, nothing is published through them.
static std::atomic<uint32_t> s_reads{0}, s_writes{0}, s_fails{0}, s_lastFail{0};
// How the host actually asks: callbacks and the largest buffer it offered.
// Sectors-per-call is the number that decides whether multi-block helps.
static std::atomic<uint32_t> s_calls{0}, s_maxBuf{0};

static inline void bump(std::atomic<uint32_t>& c, uint32_t n = 1) {
    c.fetch_add(n, std::memory_order_relaxed);
}

// One retry: an SPI read can lose an arbitration against the display, which
// shares SPI2_HOST with the card, and a second attempt costs a millisecond.
static bool readRun(uint32_t sector, uint8_t* dst, uint32_t count) {
    if (store::sdRawRead(s_pdrv, dst, sector, count)) return true;
    if (store::sdRawRead(s_pdrv, dst, sector, count)) return true;
    bump(s_fails, count);
    s_lastFail.store(sector, std::memory_order_relaxed);
    return false;
}

static bool writeRun(uint32_t sector, const uint8_t* src, uint32_t count) {
    if (store::sdRawWrite(s_pdrv, src, sector, count)) return true;
    if (store::sdRawWrite(s_pdrv, src, sector, count)) return true;
    bump(s_fails, count);
    s_lastFail.store(sector, std::memory_order_relaxed);
    return false;
}

static bool readSector(uint32_t sector, uint8_t* dst) { return readRun(sector, dst, 1); }
static bool writeSector(uint32_t sector, uint8_t* src) { return writeRun(sector, src, 1); }

// `offset` is a byte offset into the run starting at `lba`, and `bufsize` need
// not be a whole number of sectors -- TinyUSB splits a transfer to fit its
// endpoint buffer. Serving whole sectors from `lba` and ignoring `offset`
// returns the wrong bytes for any split transfer, and returns nothing at all
// when bufsize < 512, which the host reads as a failed sector.
static int32_t onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
    if (s_pdrv == 0xFF) return -1;
    uint8_t* out = (uint8_t*)buffer;

    bump(s_calls);
    if (bufsize > s_maxBuf.load(std::memory_order_relaxed))
        s_maxBuf.store(bufsize, std::memory_order_relaxed);

    // The overwhelmingly common shape: whole sectors from the start of the run.
    // One CMD18 for the lot instead of a CMD17 per 512 bytes.
    if (offset == 0 && bufsize && (bufsize % s_sectorSize) == 0) {
        uint32_t count = bufsize / s_sectorSize;
        if (!readRun(lba, out, count)) return -1;
        bump(s_reads, count);
        return (int32_t)bufsize;
    }

    uint8_t sec[512];
    uint32_t done = 0;
    while (done < bufsize) {
        uint32_t pos = offset + done;
        uint32_t sector = lba + pos / s_sectorSize;
        uint32_t within = pos % s_sectorSize;
        uint32_t n = s_sectorSize - within;
        if (n > bufsize - done) n = bufsize - done;

        if (within == 0 && n == s_sectorSize) {
            if (!readSector(sector, out + done)) return -1;
        } else {
            if (!readSector(sector, sec)) return -1;
            memcpy(out + done, sec + within, n);
        }
        done += n;
        bump(s_reads);
    }
    return (int32_t)done;
}

static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
    if (s_pdrv == 0xFF) return -1;

    if (offset == 0 && bufsize && (bufsize % s_sectorSize) == 0) {
        uint32_t count = bufsize / s_sectorSize;
        if (!writeRun(lba, buffer, count)) return -1;
        bump(s_writes, count);
        return (int32_t)bufsize;
    }

    uint8_t sec[512];
    uint32_t done = 0;
    while (done < bufsize) {
        uint32_t pos = offset + done;
        uint32_t sector = lba + pos / s_sectorSize;
        uint32_t within = pos % s_sectorSize;
        uint32_t n = s_sectorSize - within;
        if (n > bufsize - done) n = bufsize - done;

        if (within == 0 && n == s_sectorSize) {
            if (!writeSector(sector, buffer + done)) return -1;
        } else {
            // Partial sector: read, patch, write back. Writing a whole sector
            // from a fragment would destroy the bytes either side of it.
            if (!readSector(sector, sec)) return -1;
            memcpy(sec + within, buffer + done, n);
            if (!writeSector(sector, sec)) return -1;
        }
        done += n;
        bump(s_writes);
    }
    return (int32_t)done;
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
    // sdcard_init() allocates a driver slot; it does not address the card. Until
    // something runs ff_sd_initialize() the card is still in idle, the geometry
    // reads back as zero sectors, and every raw read fails -- so this registered
    // a zero-sized volume and the host, quite reasonably, showed nothing.
    uint8_t pdrv = initCard();
    if (pdrv == 0xFF) {
        os::logf("usbdisk: no usable card at boot - USB drive unavailable");
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
    s_pdrv = initCard();
    if (s_pdrv == 0xFF) {
        os::logf("usbdisk: could not claim the card");
        SPI.end();
        return false;
    }
    for (auto* c : {&s_reads, &s_writes, &s_fails, &s_lastFail, &s_calls, &s_maxBuf})
        c->store(0, std::memory_order_relaxed);
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

static uint32_t le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t le16(const uint8_t* p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }

void logGeometry() {
    if (s_pdrv == 0xFF) { os::logf("usbdisk: not attached"); return; }
    uint8_t sec[512];
    if (!readSector(0, sec)) { os::logf("usbdisk: MBR unreadable"); return; }
    os::logf("mbr: signature %02X%02X", sec[510], sec[511]);

    // First partition entry lives at 446; type at +4, first LBA at +8.
    uint8_t type = sec[446 + 4];
    uint32_t start = le32(sec + 446 + 8);
    uint32_t psize = le32(sec + 446 + 12);
    os::logf("mbr: part1 type 0x%02X, start LBA %u, %u sectors (%u MB)",
             type, (unsigned)start, (unsigned)psize,
             (unsigned)((uint64_t)psize / 2048));

    if (!readSector(start, sec)) { os::logf("usbdisk: VBR unreadable"); return; }
    uint16_t bytesPerSec = le16(sec + 11);
    uint8_t  secPerClus  = sec[13];
    uint16_t reserved    = le16(sec + 14);
    uint8_t  numFats     = sec[16];
    uint32_t fatSz32     = le32(sec + 36);
    uint32_t totSec32    = le32(sec + 32);
    os::logf("vbr: %u B/sector, %u sectors/cluster, %u reserved, %u FATs",
             bytesPerSec, secPerClus, reserved, numFats);
    os::logf("vbr: FAT is %u sectors (%u MB each, %u MB total)",
             (unsigned)fatSz32, (unsigned)(fatSz32 / 2048),
             (unsigned)((uint64_t)fatSz32 * numFats / 2048));
    if (secPerClus && bytesPerSec) {
        uint64_t dataSec = (uint64_t)totSec32 - reserved - (uint64_t)fatSz32 * numFats;
        os::logf("vbr: %u total sectors, ~%u clusters of %u KB",
                 (unsigned)totSec32, (unsigned)(dataSec / secPerClus),
                 (unsigned)(secPerClus * bytesPerSec / 1024));
    }
}

bool available() { return s_available; }
bool attached() { return s_attached; }
uint32_t sectorCount() { return s_sectors; }
uint64_t sizeMB() { return (uint64_t)s_sectors * s_sectorSize / (1024ULL * 1024ULL); }
uint32_t readCount() { return s_reads.load(std::memory_order_relaxed); }
uint32_t failCount() { return s_fails.load(std::memory_order_relaxed); }
uint32_t callCount() { return s_calls.load(std::memory_order_relaxed); }
uint32_t maxBufsize() { return s_maxBuf.load(std::memory_order_relaxed); }
uint32_t lastFailLba() { return s_lastFail.load(std::memory_order_relaxed); }
uint32_t writeCount() { return s_writes.load(std::memory_order_relaxed); }

#else   // ARDUINO_USB_MODE == 1: hardware CDC/JTAG, no TinyUSB, no MSC

void begin() { os::logf("usbdisk: needs ARDUINO_USB_MODE=0 (TinyUSB)"); }
bool available() { return false; }
bool attached() { return false; }
uint32_t sectorCount() { return 0; }
uint64_t sizeMB() { return 0; }
uint32_t readCount() { return 0; }
uint32_t failCount() { return 0; }
uint32_t callCount() { return 0; }
uint32_t maxBufsize() { return 0; }
void logGeometry() { os::logf("usbdisk: needs the USB-drive build"); }
uint32_t lastFailLba() { return 0; }
uint32_t writeCount() { return 0; }
bool attach() { return false; }
void detach() {}

#endif

}  // namespace usbdisk
