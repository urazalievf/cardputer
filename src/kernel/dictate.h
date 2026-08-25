// One blocking "talk to me" for the apps that only ever wanted words back.
//
// Voice drives capture from tick() because it draws a live waveform and offers
// the audio afterwards. Translate and Ask do not: they press, talk, press, and
// use the transcript. Both had their own copy of the loop, both were capped at
// whatever fitted in RAM, and both stopped a chunk after they started because
// the key that began the recording was still down.
#pragma once
#include "os.h"
#include "ui.h"

namespace dictate {

struct Result {
    bool ok = false;
    String text;
    String error;
    String wavPath;      // where the audio was kept, empty if it was not
};

// Records with a live level meter until a key is pressed -- armed only once
// every key has been released -- then transcribes. Streams to the card when the
// card allows it, so the length is not bounded by the free heap. Leaves the
// canvas and the microphone as it found them.
Result run(const String& title, ui::Icon icon = ui::Icon::Mic);

}  // namespace dictate
