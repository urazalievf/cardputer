// A line-oriented command console on the USB serial port.
//
// Exists so credentials can be provisioned from a real keyboard. Typing an API
// key on 56 keys the size of rice grains is miserable, and the alternative --
// putting them in the repository -- leaks them permanently.
#pragma once
#include "os.h"

namespace console {
void poll();      // non-blocking; call once per event loop pass
}
