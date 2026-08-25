// USB Mass Storage: hand the SD card to the host over the same cable that
// powers the device, so it mounts in Finder like any other card reader.
//
// The card cannot be owned by both sides at once — a filesystem mounted here
// while the Mac writes to it corrupts. So this is a mode, not a background
// service: turning it on unmounts the device's own view of the card.
#pragma once
#include "os.h"

namespace usbdisk {

// Called once at boot. Probes the card for its geometry and registers the MSC
// interface with the media reported absent. The interface has to exist before
// USB enumerates, which is why it cannot be created on demand later.
void begin();

bool available();          // a card was found at boot and MSC is registered
bool attached();           // the host currently sees media
uint32_t sectorCount();
uint64_t sizeMB();
uint32_t readCount();
// Sectors the host asked for that we could not serve, and the last one that
// failed. A volume that identifies but will not mount is almost always this.
uint32_t failCount();
uint32_t lastFailLba();
uint32_t callCount();
uint32_t maxBufsize();
// Decode the MBR and the FAT32 BPB to the log. How much FAT a volume has is
// what decides whether a host can mount it over a Full-Speed link at all.
void logGeometry();
uint32_t writeCount();

bool attach();             // give the card to the host
void detach();             // take it back

}  // namespace usbdisk
