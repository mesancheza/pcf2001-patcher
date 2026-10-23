// The four functions every graphics backend of the game exports (see docs/BACKEND-INTERFACE.md).
#include "backend_log.h"
#include "device.h"
#include "diagnostics.h"
#include "memory_compat.h"
#include "session_info.h"

#include <new>

namespace
{
constexpr const char* kAdapterName = "Direct3D 11";
}

extern "C" int __cdecl NumeroDispositivos()
{
    return 1;
}

extern "C" const char* __cdecl DescripcionDispositivo(int index)
{
    return index == 0 ? kAdapterName : nullptr;
}

extern "C" void* __cdecl CrearDevice(int index)
{
    if (index != 0)
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
