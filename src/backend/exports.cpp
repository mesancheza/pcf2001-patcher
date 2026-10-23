// The four functions every graphics backend of the game exports (see docs/BACKEND-INTERFACE.md).
#include "backend_log.h"
#include "device.h"
#include "diagnostics.h"
#include "memory_compat.h"
#include "session_info.h"

#include <cstring>
#include <new>

namespace
{
constexpr const char* kAdapterName = "Direct3D 11";

// As many devices as the game's 3D backend, dcd3d.dll, reports (one per DirectDraw device: usually "Primary Display
// Driver" plus one per graphics card). The game mixes device numbers between backends: on its first start, with no
// MainOpt.dat yet, it asks the 2D backend for the 3D backend's last device (docs/GAME-INTERNALS.md, "Default 3D
// settings"). All of ours are the same Direct3D 11 output.
int DeviceCount()
{
    static const int count = [] {
        int devices = 1;
        HMODULE self = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&DeviceCount), &self);
        char path[MAX_PATH];
        GetModuleFileNameA(self, path, MAX_PATH);
        char* slash = strrchr(path, '\\');
        strcpy_s(slash ? slash + 1 : path, MAX_PATH - (slash ? slash + 1 - path : 0), "dcd3d.dll");
        // Left loaded: the game loads it anyway, as its 3D backend.
        if (HMODULE d3d = LoadLibraryA(path))
        {
            using CountFn = int(__cdecl*)();
            if (auto count3D = reinterpret_cast<CountFn>(GetProcAddress(d3d, "NumeroDispositivos")))
                devices = count3D();
        }
        if (devices < 1)
            devices = 1;
        BackendLog("devices: %d (as many as dcd3d.dll)\n", devices);
        return devices;
    }();
    return count;
}
}  // namespace

extern "C" int __cdecl NumeroDispositivos()
{
    return DeviceCount();
}

extern "C" const char* __cdecl DescripcionDispositivo(int index)
{
    return index >= 0 && index < DeviceCount() ? kAdapterName : nullptr;
}

extern "C" void* __cdecl CrearDevice(int index)
{
    if (index < 0 || index >= DeviceCount())
        return nullptr;
    static bool sessionLogged;
    if (!sessionLogged)
        LogSessionInfo();
    sessionLogged = true;
    // First call into this DLL after it's loaded, before the game loads its database: a good moment to protect its heap.
    InstallMemoryCompat();
    // Return the interface pointer: gfxwin.dll calls its methods through the vtable at offset 0.
    DeviceInterface* device = new (std::nothrow) Device();
    return device;
}

extern "C" void __cdecl DestruirDevice(void** device)
{
    if (!device || !*device)
        return;
    // Not freed: the game keeps calling into destroyed devices while it shuts down (docs/GAME-INTERNALS.md, "Crash on
    // exit"). The zombie releases its resources and ignores further calls; the memory goes away with the process.
    BackendLog("DestruirDevice: device %p kept as a zombie\n", *device);
    static_cast<Device*>(static_cast<DeviceInterface*>(*device))->MakeZombie();
    *device = nullptr;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_DETACH)
    {
        LogProcessExit(reserved != nullptr);
        return TRUE;
    }
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(instance);
        InitBackendLog(instance);
        InstallDiagnostics(instance);
        // The game computes for long stretches without processing window messages (e.g. simulating a match day behind
        // its progress window). In exclusive full screen Windows never cared; for a normal window it would replace it
        // with a "not responding" ghost and offer to close the game.
        DisableProcessWindowsGhosting();
        // Device objects may outlive the game's own reference to this DLL (see DestruirDevice), so never unload.
        HMODULE pinned;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCSTR>(&DllMain), &pinned);
    }
    return TRUE;
}
