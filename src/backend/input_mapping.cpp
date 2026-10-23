#include "input_mapping.h"

#include "backend_log.h"
#include "presenter.h"

#include <windowsx.h>

#include <algorithm>
#include <cstring>

namespace
{
// Window property holding the procedure MappedWindowProc forwards to. It is set as long as MappedWindowProc is in that
// window's chain, which outlives UninstallInputMapping when another DLL (the match's d2s2.dll) subclassed the window
// after us: hooking such a window again would make the two procedures call each other forever.
constexpr const char* kPreviousProcProperty = "pcf2001-patch.wndproc";

HWND g_window;  // window whose input is mapped
volatile LONG g_frameWidth;
volatile LONG g_frameHeight;
bool g_importsPatched;
bool g_forceRect;
RECT g_forcedRect;

// The real functions, resolved from user32 directly (this DLL's own imports are never patched).
using GetCursorPosFn = BOOL(WINAPI*)(LPPOINT);
using SetCursorPosFn = BOOL(WINAPI*)(int, int);
using ClipCursorFn = BOOL(WINAPI*)(const RECT*);
using PointFn = BOOL(WINAPI*)(HWND, LPPOINT);
using GetClientRectFn = BOOL(WINAPI*)(HWND, LPRECT);
GetCursorPosFn g_getCursorPos;
SetCursorPosFn g_setCursorPos;
ClipCursorFn g_clipCursor;
PointFn g_screenToClient;
PointFn g_clientToScreen;
GetClientRectFn g_getClientRect;

struct Mapping
{
    bool active;
    RECT fit;          // where the frame is drawn, in real client coordinates
    POINT origin;      // real client origin in screen coordinates
    int frameWidth;
    int frameHeight;
};

Mapping CurrentMapping()
{
    Mapping m{};
    m.frameWidth = g_frameWidth;
    m.frameHeight = g_frameHeight;
    if (!g_window || m.frameWidth <= 0 || m.frameHeight <= 0)
        return m;
    RECT client{};
    g_getClientRect(g_window, &client);
    m.origin = POINT{0, 0};
    g_clientToScreen(g_window, &m.origin);
    m.fit = Presenter::FitRect(client.right, client.bottom, m.frameWidth, m.frameHeight);
    m.active = client.right != m.frameWidth || client.bottom != m.frameHeight;
    return m;
}

POINT RealClientToFrame(const Mapping& m, POINT p)
{
    const int width = std::max<LONG>(m.fit.right - m.fit.left, 1);
    const int height = std::max<LONG>(m.fit.bottom - m.fit.top, 1);
    POINT f;
    f.x = static_cast<LONG>((static_cast<int64_t>(p.x - m.fit.left) * m.frameWidth) / width);
    f.y = static_cast<LONG>((static_cast<int64_t>(p.y - m.fit.top) * m.frameHeight) / height);
    f.x = std::clamp<LONG>(f.x, 0, m.frameWidth - 1);
    f.y = std::clamp<LONG>(f.y, 0, m.frameHeight - 1);
    return f;
}

POINT FrameToRealClient(const Mapping& m, POINT f)
{
    POINT p;
    p.x = m.fit.left + static_cast<LONG>((static_cast<int64_t>(f.x) * (m.fit.right - m.fit.left)) / m.frameWidth);
    p.y = m.fit.top + static_cast<LONG>((static_cast<int64_t>(f.y) * (m.fit.bottom - m.fit.top)) / m.frameHeight);
    return p;
}

// --- API replacements, installed in gfxwin.dll's and manager.exe's imports ---

BOOL WINAPI MappedGetCursorPos(LPPOINT point)
{
    if (!g_getCursorPos(point))
        return FALSE;
    const Mapping m = CurrentMapping();
    if (m.active)
    {
        POINT client{point->x - m.origin.x, point->y - m.origin.y};
        const POINT frame = RealClientToFrame(m, client);
        point->x = m.origin.x + frame.x;
        point->y = m.origin.y + frame.y;
    }
    return TRUE;
}

BOOL WINAPI MappedSetCursorPos(int x, int y)
{
    const Mapping m = CurrentMapping();
    if (m.active)
    {
        const POINT client = FrameToRealClient(m, POINT{x - m.origin.x, y - m.origin.y});
        x = m.origin.x + client.x;
        y = m.origin.y + client.y;
    }
    return g_setCursorPos(x, y);
}

BOOL WINAPI MappedClipCursor(const RECT* rect)
{
    const Mapping m = CurrentMapping();
    if (!rect || !m.active)
        return g_clipCursor(rect);
    const POINT topLeft = FrameToRealClient(m, POINT{rect->left - m.origin.x, rect->top - m.origin.y});
    const POINT bottomRight = FrameToRealClient(m, POINT{rect->right - m.origin.x, rect->bottom - m.origin.y});
    const RECT real{m.origin.x + topLeft.x, m.origin.y + topLeft.y, m.origin.x + bottomRight.x, m.origin.y + bottomRight.y};
    return g_clipCursor(&real);
}

BOOL WINAPI MappedScreenToClient(HWND window, LPPOINT point)
{
    if (window != g_window)
        return g_screenToClient(window, point);
    const Mapping m = CurrentMapping();
    if (!m.active)
        return g_screenToClient(window, point);
    point->x -= m.origin.x;
    point->y -= m.origin.y;
    return TRUE;
}

BOOL WINAPI MappedClientToScreen(HWND window, LPPOINT point)
{
    if (window != g_window)
        return g_clientToScreen(window, point);
    const Mapping m = CurrentMapping();
    if (!m.active)
        return g_clientToScreen(window, point);
    point->x += m.origin.x;
    point->y += m.origin.y;
    return TRUE;
}

BOOL WINAPI MappedGetClientRect(HWND window, LPRECT rect)
{
    if (window != g_window)
        return g_getClientRect(window, rect);
    const Mapping m = CurrentMapping();
    if (!m.active)
        return g_getClientRect(window, rect);
    *rect = RECT{0, 0, m.frameWidth, m.frameHeight};
    return TRUE;
}

LRESULT CALLBACK MappedWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    const auto previous = reinterpret_cast<WNDPROC>(GetPropA(window, kPreviousProcProperty));
    if (message == WM_NCDESTROY)
    {
        RemovePropA(window, kPreviousProcProperty);
        if (window == g_window)
            g_window = nullptr;
    }
    if (!previous)
        return DefWindowProcA(window, message, wParam, lParam);
    // Still in the chain of a window whose input isn't mapped any more: only pass messages on.
    if (window != g_window)
        return CallWindowProcA(previous, window, message, wParam, lParam);

    if (message == WM_WINDOWPOSCHANGING && g_forceRect)
    {
        auto* position = reinterpret_cast<WINDOWPOS*>(lParam);
        position->x = g_forcedRect.left;
        position->y = g_forcedRect.top;
        position->cx = g_forcedRect.right - g_forcedRect.left;
        position->cy = g_forcedRect.bottom - g_forcedRect.top;
        position->flags &= ~(SWP_NOMOVE | SWP_NOSIZE);
    }
    // In exclusive full screen Windows took the screen back from the game on Alt+Tab. Our full screen is a normal
    // window, so the topmost z-order gfxwin asks for would hold the screen hostage.
    if (message == WM_WINDOWPOSCHANGING)
    {
        auto* position = reinterpret_cast<WINDOWPOS*>(lParam);
        if (!(position->flags & SWP_NOZORDER) && position->hwndInsertAfter == HWND_TOPMOST)
            position->hwndInsertAfter = HWND_NOTOPMOST;
    }

    const Mapping m = CurrentMapping();
    if (m.active)
    {
        switch (message)
        {
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
        case WM_XBUTTONDBLCLK:
        {
            const POINT frame = RealClientToFrame(m, POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            lParam = MAKELPARAM(frame.x, frame.y);
            break;
        }
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
        {
            const POINT frame = RealClientToFrame(m, POINT{GET_X_LPARAM(lParam) - m.origin.x, GET_Y_LPARAM(lParam) - m.origin.y});
            lParam = MAKELPARAM(m.origin.x + frame.x, m.origin.y + frame.y);
            break;
        }
        case WM_SIZE:
            // The game keeps believing its client area is the frame size.
            lParam = MAKELPARAM(m.frameWidth, m.frameHeight);
            break;
        default:
            break;
        }
    }
    return CallWindowProcA(previous, window, message, wParam, lParam);
}

int PatchImports(HMODULE module, const char* function, void* replacement)
{
    if (!module)
        return 0;
    auto* base = reinterpret_cast<BYTE*>(module);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
    const IMAGE_DATA_DIRECTORY& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress)
        return 0;
    int patched = 0;
    for (auto* import = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress); import->Name; ++import)
    {
        if (!import->OriginalFirstThunk)
            continue;
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + import->OriginalFirstThunk);
        auto* addresses = reinterpret_cast<IMAGE_THUNK_DATA*>(base + import->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++addresses)
        {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
                continue;
            const auto* byName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (strcmp(reinterpret_cast<const char*>(byName->Name), function) != 0)
                continue;
            DWORD protection;
            VirtualProtect(&addresses->u1.Function, sizeof addresses->u1.Function, PAGE_READWRITE, &protection);
            addresses->u1.Function = reinterpret_cast<ULONG_PTR>(replacement);
            VirtualProtect(&addresses->u1.Function, sizeof addresses->u1.Function, protection, &protection);
            ++patched;
        }
    }
    return patched;
}

void PatchGameImports()
{
    if (g_importsPatched)
        return;
    HMODULE user32 = GetModuleHandleA("user32.dll");
    g_getCursorPos = reinterpret_cast<GetCursorPosFn>(GetProcAddress(user32, "GetCursorPos"));
    g_setCursorPos = reinterpret_cast<SetCursorPosFn>(GetProcAddress(user32, "SetCursorPos"));
    g_clipCursor = reinterpret_cast<ClipCursorFn>(GetProcAddress(user32, "ClipCursor"));
    g_screenToClient = reinterpret_cast<PointFn>(GetProcAddress(user32, "ScreenToClient"));
    g_clientToScreen = reinterpret_cast<PointFn>(GetProcAddress(user32, "ClientToScreen"));
    g_getClientRect = reinterpret_cast<GetClientRectFn>(GetProcAddress(user32, "GetClientRect"));

    int patched = 0;
    for (const char* name : {"gfxwin.dll", "manager.exe"})
    {
        HMODULE module = GetModuleHandleA(name);
        patched += PatchImports(module, "GetCursorPos", reinterpret_cast<void*>(&MappedGetCursorPos));
        patched += PatchImports(module, "SetCursorPos", reinterpret_cast<void*>(&MappedSetCursorPos));
        patched += PatchImports(module, "ClipCursor", reinterpret_cast<void*>(&MappedClipCursor));
        patched += PatchImports(module, "ScreenToClient", reinterpret_cast<void*>(&MappedScreenToClient));
        patched += PatchImports(module, "ClientToScreen", reinterpret_cast<void*>(&MappedClientToScreen));
        patched += PatchImports(module, "GetClientRect", reinterpret_cast<void*>(&MappedGetClientRect));
    }
    g_importsPatched = true;
    BackendLog("input mapping: %d imports redirected\n", patched);
}
}  // namespace

void InstallInputMapping(HWND window)
{
    PatchGameImports();
    if (g_window == window)
        return;
    UninstallInputMapping();
    // Our procedure may still be in this window's chain, under another one: then it only has to start mapping again.
    if (!GetPropA(window, kPreviousProcProperty))
    {
        const auto previous = GetWindowLongPtrA(window, GWLP_WNDPROC);
        SetPropA(window, kPreviousProcProperty, reinterpret_cast<HANDLE>(previous));
        SetWindowLongPtrA(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&MappedWindowProc));
    }
    g_window = window;
}

void UninstallInputMapping()
{
    if (!g_window)
        return;
    // Only unhook if nobody subclassed the window after us; otherwise our procedure stays in the chain, passing
    // messages on.
    if (reinterpret_cast<WNDPROC>(GetWindowLongPtrA(g_window, GWLP_WNDPROC)) == &MappedWindowProc)
    {
        SetWindowLongPtrA(g_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(GetPropA(g_window, kPreviousProcProperty)));
        RemovePropA(g_window, kPreviousProcProperty);
    }
    g_window = nullptr;
}

void SetInputFrameSize(int width, int height)
{
    InterlockedExchange(&g_frameWidth, width);
    InterlockedExchange(&g_frameHeight, height);
}

void SetForcedWindowRect(const RECT* rect)
{
    g_forceRect = rect != nullptr;
    if (rect)
        g_forcedRect = *rect;
}
