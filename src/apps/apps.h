#pragma once
#include "../kernel/os.h"

// Every app is a singleton owned by its translation unit.
App* launcherApp();
App* notesApp();
App* voiceApp();
App* askApp();
App* codeApp();
App* wifiApp();
App* filesApp();
App* settingsApp();
