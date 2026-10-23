// Mouse and window coordinate translation for a scaled picture.
//
// The game thinks its client area is exactly the size of its frame (e.g. 800x600). When the frame is shown scaled
// (e.g. full screen as a borderless window), the presenter draws it inside a letterboxed rectangle of the real client
// area, so coordinates must be translated both ways:
//   - mouse messages sent to the game window: real client coordinates -> frame coordinates;
//   - cursor and window APIs called by gfxwin.dll and manager.exe (GetCursorPos, SetCursorPos, ClipCursor,
//     ScreenToClient, ClientToScreen, GetClientRect): the game sees a client area of the frame's size, placed at the
//     real client origin.
// When the client area is exactly the frame size, every translation is the identity.
#pragma once

#include <windows.h>

void InstallInputMapping(HWND window);
void UninstallInputMapping();

// Size of the game's frame; translation is active when the real client area differs from it.
void SetInputFrameSize(int width, int height);

// While set, the game window is kept at this screen rectangle: in full screen the game positions its window for the
// display mode it believes it set (e.g. 800x600 at the top-left corner), but the display mode is never changed, so the
// window must keep covering the monitor. Null releases the window.
void SetForcedWindowRect(const RECT* rect);
