// Mouse wheel scrolling for the game's lists (see docs/GAME-INTERNALS.md, "Mouse wheel").
//
// The game predates the mouse wheel and ignores WM_MOUSEWHEEL. Its lists scroll with gfxwin.dll's scroll bar control,
// whose arrow buttons call CGFXScrollBarControl::SBPressed: that moves the bar one step and tells the list's dialog,
// which redraws the list. A wheel notch does the same: it finds the vertical scroll bar nearest to the control under
// the mouse and presses its up or down arrow, once per line the Windows setting asks for. Everything it uses is
// looked up by name in gfxwin.dll's exports; if something isn't found, the wheel does nothing.
#pragma once

#include <windows.h>

// Handles a WM_MOUSEWHEEL for the game window: `delta` is the wheel's movement (positive away from the user) and
// `point` the cursor in the game's frame coordinates. Call it on the game's thread, from its window procedure.
void ScrollListUnderMouse(int delta, POINT point);
