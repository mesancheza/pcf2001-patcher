// The installer's wizard: welcome, CD, folder, updates, progress, end.
#pragma once

#include <windows.h>

// Shows the wizard and installs. Returns the process exit code (0 when the game was installed).
int RunWizard(HINSTANCE instance);
