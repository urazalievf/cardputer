#pragma once
#include "../kernel/os.h"

App* launcherApp();
App* notesApp();
App* voiceApp();
App* askApp();
App* codeApp();
App* wifiApp();
App* bluetoothApp();
App* filesApp();
App* calcApp();
App* timerApp();
App* translateApp();
App* tasksApp();
App* weatherApp();
App* remoteApp();
App* shareApp();
App* settingsApp();

// Shared picker: flattens "which assistant" into one list — the agent CLIs the
// Mac daemon has (account logins, no API key) plus every key-based provider.
// Returns true if the selection changed.
bool chooseAssistant();
