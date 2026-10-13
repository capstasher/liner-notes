#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace ln {
class Plugin;
const UINT WM_APP_DIALOG_REFRESH = WM_APP + 20;
// Modal; returns when the user closes the window.
void runLinerNotesDialog(Plugin* plugin, HWND owner, int driveIdx);
}
