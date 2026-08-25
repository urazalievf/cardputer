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
uint32_t writeCount();

bool attach();             // give the card to the host
void detach();             // take it back

}  // namespace usbdisk
