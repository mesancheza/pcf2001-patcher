#include "wheel_scroll.h"

#include "backend_log.h"

#include <climits>
#include <cstdint>

namespace
{
// gfxwin.dll's CGFXWindow, as its own code uses it (CGFXWindow::PointWindow, CGFXScrollBarControl::SetPos).
constexpr size_t kParent = 0x04;
constexpr size_t kFirstChild = 0x0C;
constexpr size_t kNextSibling = 0x10;
constexpr size_t kLeft = 0x4C;  // the window's rectangle, in its parent's coordinates
constexpr size_t kTop = 0x50;
constexpr size_t kRight = 0x54;
constexpr size_t kBottom = 0x58;
constexpr size_t kShown = 0x144;  // byte

// A list's controls are a handful of levels deep; these only bound the search against a damaged tree.
constexpr int kMaxDepth = 16;
constexpr int kMaxVisited = 4096;

// The exports used, looked up by name. The members are __thiscall: called as __fastcall, whose first argument goes in
// ecx like `this`, with a dummy second one for edx.
using WindowFn = void*(__cdecl*)();                                // CGFXWindow::MouseWindow, ::ModalWindow (static)
using ButtonFn = void*(__fastcall*)(void* bar, void* edx);         // CGFXScrollBarControl::BtnTL, ::BtnBR
using PressFn = void(__fastcall*)(void* bar, void* edx, void* button);  // CGFXScrollBarControl::SBPressed

// Virtual methods of CGFXScrollBarControl that manager.exe's own scroll bar classes, derived from it, keep: their
// vtables point at them through manager.exe's import stubs.
constexpr const char* kScrollBarMethods[] = {
    "?Move@CGFXScrollBarControl@@UAEXABVCRectangulo@@H@Z",
    "?GFXEvMouseIn@CGFXScrollBarControl@@UAEXVCPunto@@G@Z",
    "?GFXEvMouseOut@CGFXScrollBarControl@@UAEXVCPunto@@G@Z",
    "?PaintWindow@CGFXScrollBarControl@@UAEXAAVCGFXDrawContext@@@Z",
    "?Create@CGFXScrollBarControl@@UAEHPAVCGFXWindow@@ABVCRectangulo@@PBDKKVCColor@@3H@Z",
};
constexpr int kVtableSlots = 64;  // enough for CGFXWindow's virtual methods and a derived class's own

struct Gfxwin
{
    bool resolved;
    bool available;
    const void* scrollBarVtable;  // CGFXScrollBarControl's, to recognise its instances
    const void* scrollBarMethods[sizeof kScrollBarMethods / sizeof kScrollBarMethods[0]];
    const void* knownVtables[16];  // vtables already found to be a scroll bar's (derived classes)
    int knownCount;
    // The images of manager.exe and gfxwin.dll: a vtable's entries are only followed while they point into them, since
    // nothing marks where a vtable ends.
    const uint8_t* imageStart[2];
    const uint8_t* imageEnd[2];
    WindowFn mouseWindow;
    WindowFn modalWindow;
    ButtonFn upButton;
    ButtonFn downButton;
    PressFn press;
};
Gfxwin g_gfxwin;
int g_pendingDelta;  // wheel movement not yet a whole notch (high-resolution wheels send less than WHEEL_DELTA)

void ResolveGfxwin()
{
    if (g_gfxwin.resolved)
        return;
    g_gfxwin.resolved = true;
    HMODULE gfxwin = GetModuleHandleA("gfxwin.dll");
    if (!gfxwin)
    {
        BackendLog("mouse wheel: gfxwin.dll not loaded, lists won't scroll with it\n");
        return;
    }
    g_gfxwin.scrollBarVtable = reinterpret_cast<const void*>(GetProcAddress(gfxwin, "??_7CGFXScrollBarControl@@6B@"));
    g_gfxwin.mouseWindow = reinterpret_cast<WindowFn>(GetProcAddress(gfxwin, "?MouseWindow@CGFXWindow@@SAPAV1@XZ"));
    g_gfxwin.modalWindow = reinterpret_cast<WindowFn>(GetProcAddress(gfxwin, "?ModalWindow@CGFXWindow@@SAPAV1@XZ"));
    g_gfxwin.upButton = reinterpret_cast<ButtonFn>(
        GetProcAddress(gfxwin, "?BtnTL@CGFXScrollBarControl@@QAEAAVCGFXButtonControl@@XZ"));
    g_gfxwin.downButton = reinterpret_cast<ButtonFn>(
        GetProcAddress(gfxwin, "?BtnBR@CGFXScrollBarControl@@QAEAAVCGFXButtonControl@@XZ"));
    g_gfxwin.press = reinterpret_cast<PressFn>(
        GetProcAddress(gfxwin, "?SBPressed@CGFXScrollBarControl@@IAEXAAVCGFXSBButton@@@Z"));
    const HMODULE images[2] = {GetModuleHandleA(nullptr), gfxwin};
    for (int i = 0; i < 2; ++i)
    {
        const auto* base = reinterpret_cast<const uint8_t*>(images[i]);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
        g_gfxwin.imageStart[i] = base;
        g_gfxwin.imageEnd[i] = base + nt->OptionalHeader.SizeOfImage;
    }
    bool methods = true;
    for (size_t i = 0; i < sizeof kScrollBarMethods / sizeof kScrollBarMethods[0]; ++i)
    {
        g_gfxwin.scrollBarMethods[i] = reinterpret_cast<const void*>(GetProcAddress(gfxwin, kScrollBarMethods[i]));
        methods = methods && g_gfxwin.scrollBarMethods[i];
    }
    g_gfxwin.available = g_gfxwin.scrollBarVtable && methods && g_gfxwin.mouseWindow && g_gfxwin.modalWindow &&
                         g_gfxwin.upButton && g_gfxwin.downButton && g_gfxwin.press;
    BackendLog(g_gfxwin.available ? "mouse wheel: scrolls the lists\n"
                                  : "mouse wheel: gfxwin.dll exports not found, lists won't scroll with it\n");
}

uint8_t* Field(void* window, size_t offset)
{
    return static_cast<uint8_t*>(window) + offset;
}

void* Pointer(void* window, size_t offset)
{
    return *reinterpret_cast<void**>(Field(window, offset));
}

int Int(void* window, size_t offset)
{
    return *reinterpret_cast<int*>(Field(window, offset));
}

bool InGameImage(const void* address)
{
    const auto* p = static_cast<const uint8_t*>(address);
    for (int i = 0; i < 2; ++i)
    {
        if (p >= g_gfxwin.imageStart[i] && p + 6 <= g_gfxwin.imageEnd[i])
            return true;
    }
    return false;
}

// The function an import stub (jmp dword ptr [address]) jumps to, or the address itself if it isn't one.
const void* ThroughStub(const void* code)
{
    const auto* bytes = static_cast<const uint8_t*>(code);
    if (bytes[0] == 0xFF && bytes[1] == 0x25)
        return **reinterpret_cast<const void* const* const*>(bytes + 2);
    return code;
}

// gfxwin's own scroll bars have CGFXScrollBarControl's vtable; manager.exe's, of classes derived from it, have their
// own, which still point at some of its methods.
bool IsScrollBar(void* window)
{
    const void* const* vtable = *static_cast<const void* const**>(window);
    if (vtable == g_gfxwin.scrollBarVtable)
        return true;
    for (int i = 0; i < g_gfxwin.knownCount; ++i)
    {
        if (g_gfxwin.knownVtables[i] == vtable)
            return true;
    }
    for (int slot = 0; slot < kVtableSlots && InGameImage(vtable[slot]); ++slot)
    {
        const void* method = ThroughStub(vtable[slot]);
        for (const void* scrollBarMethod : g_gfxwin.scrollBarMethods)
        {
            if (method != scrollBarMethod)
                continue;
            if (g_gfxwin.knownCount < static_cast<int>(sizeof g_gfxwin.knownVtables / sizeof g_gfxwin.knownVtables[0]))
                g_gfxwin.knownVtables[g_gfxwin.knownCount++] = vtable;
            return true;
        }
    }
    return false;
}

// The window's top-left corner in the root window's coordinates, which are the game's frame coordinates.
POINT TopLeft(void* window)
{
    POINT p{0, 0};
    for (int depth = 0; window && Pointer(window, kParent) && depth < kMaxDepth; ++depth)
    {
        p.x += Int(window, kLeft);
        p.y += Int(window, kTop);
        window = Pointer(window, kParent);
    }
    return p;
}

bool IsInside(void* window, void* ancestor)
{
    for (int depth = 0; window && depth < kMaxDepth; ++depth, window = Pointer(window, kParent))
    {
        if (window == ancestor)
            return true;
    }
    return false;
}

struct Search
{
    POINT point;
    void* modal;
    void* best;
    int bestScore;
    int visited;
};

// How far a vertical scroll bar is from the cursor, lower being better; INT_MAX if the bar's height doesn't span the
// cursor's, so that the wheel never moves a list somewhere else on the screen. One to the right, where lists keep
// their bars, wins over one as far to the left.
int Score(void* bar, POINT point)
{
    const POINT origin = TopLeft(bar);
    const RECT r{origin.x, origin.y, origin.x + Int(bar, kRight) - Int(bar, kLeft),
                 origin.y + Int(bar, kBottom) - Int(bar, kTop)};
    if (point.y < r.top || point.y >= r.bottom)
        return INT_MAX;
    if (point.x < r.left)
        return r.left - point.x;
    if (point.x >= r.right)
        return 2 * (point.x - r.right + 1);
    return 0;
}

// Looks for the best vertical scroll bar among the shown windows under `window`.
void SearchTree(void* window, Search& search, int depth)
{
    for (void* child = Pointer(window, kFirstChild); child && search.visited < kMaxVisited;
         child = Pointer(child, kNextSibling))
    {
        ++search.visited;
        if (!*Field(child, kShown))
            continue;
        if (IsScrollBar(child))
        {
            const bool vertical = Int(child, kBottom) - Int(child, kTop) > Int(child, kRight) - Int(child, kLeft);
            if (vertical && (!search.modal || IsInside(child, search.modal)))
            {
                const int score = Score(child, search.point);
                if (score < search.bestScore)
                {
                    search.best = child;
                    search.bestScore = score;
                }
            }
            continue;
        }
        if (depth < kMaxDepth)
            SearchTree(child, search, depth + 1);
    }
}

// The scroll bar the wheel should move: the one under the mouse, or the nearest one beside the cursor in the closest
// enclosing window that has any. Reads gfxwin's windows directly, so a damaged tree only means no scrolling.
void* FindScrollBar(POINT point)
{
    __try
    {
        void* start = g_gfxwin.mouseWindow();
        if (!start)
            return nullptr;
        void* modal = g_gfxwin.modalWindow();
        int level = 0;
        for (void* window = start; window && level < kMaxDepth; window = Pointer(window, kParent), ++level)
        {
            if (!*Field(window, kShown))
                return nullptr;  // the last window under the mouse is gone from the screen (during a match, say)
        }
        level = 0;
        for (void* window = start; window && level < kMaxDepth; window = Pointer(window, kParent), ++level)
        {
            if (IsScrollBar(window))
                return window;  // the mouse is on the bar itself (its arrows or its thumb)
        }
        int depth = 0;
        for (void* window = start; window && depth < kMaxDepth; window = Pointer(window, kParent), ++depth)
        {
            Search search{point, modal, nullptr, INT_MAX, 0};
            SearchTree(window, search, 0);
            if (search.best)
                return search.best;
        }
        return nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}

int LinesPerNotch()
{
    UINT lines = 3;
    SystemParametersInfoA(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
    if (lines == WHEEL_PAGESCROLL)
        return 10;
    return static_cast<int>(lines > 20 ? 20 : lines);
}
}  // namespace

void ScrollListUnderMouse(int delta, POINT point)
{
    ResolveGfxwin();
    if (!g_gfxwin.available)
        return;
    if ((g_pendingDelta > 0 && delta < 0) || (g_pendingDelta < 0 && delta > 0))
        g_pendingDelta = 0;  // a change of direction starts again
    g_pendingDelta += delta;
    const int notches = g_pendingDelta / WHEEL_DELTA;
    if (!notches)
        return;
    g_pendingDelta -= notches * WHEEL_DELTA;

    void* bar = FindScrollBar(point);
    if (!bar)
        return;
    // Away from the user scrolls up, as in every Windows list.
    void* button = notches > 0 ? g_gfxwin.upButton(bar, nullptr) : g_gfxwin.downButton(bar, nullptr);
    const int presses = (notches > 0 ? notches : -notches) * LinesPerNotch();
    for (int i = 0; i < presses; ++i)
        g_gfxwin.press(bar, nullptr, button);
}
