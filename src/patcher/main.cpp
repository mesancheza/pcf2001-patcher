// pcf2001-patch.exe: applies the fixes for modern Windows to an installed PC FÚTBOL 2001 v3.6 (see docs/PATCHER.md).
//
//   pcf2001-patch.exe                      the window
//   pcf2001-patch.exe /aplicar [<carpeta>] applies the patch without asking anything; exit code 0 when applied
//   pcf2001-patch.exe /quitar [<carpeta>]  removes it the same way
// Without a folder, the game is found through the registry value its installer wrote.
#include "patch.h"
#include "resource.h"
#include "system_integration.h"
#include "text.h"

#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <string>
#include <vector>

namespace
{
std::filesystem::path g_folder;

std::filesystem::path PickFolder(HWND owner)
{
    std::filesystem::path result;
    IFileOpenDialog* picker = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&picker))))
        return result;
    DWORD options = 0;
    picker->GetOptions(&options);
    picker->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    picker->SetTitle(L"Carpeta de PC FÚTBOL 2001");
    IShellItem* item = nullptr;
    if (SUCCEEDED(picker->Show(owner)) && SUCCEEDED(picker->GetResult(&item)))
    {
        PWSTR path = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
            result = path;
        CoTaskMemFree(path);
        item->Release();
    }
    picker->Release();
    return result;
}

void Refresh(HWND dialog)
{
    SetDlgItemTextW(dialog, IDC_FOLDER, g_folder.c_str());
    const PatchStatus status = g_folder.empty() ? PatchStatus{PatchState::NotAGame, L"Elige la carpeta donde está instalado el juego."}
                                                : CheckGame(g_folder);
    SetDlgItemTextW(dialog, IDC_STATE, status.details.c_str());
    const bool canApply = status.state == PatchState::Unpatched || status.state == PatchState::PatchedOther;
    const bool canRemove = status.state == PatchState::Patched || status.state == PatchState::PatchedOther;
    SetDlgItemTextW(dialog, IDC_APPLY, status.state == PatchState::PatchedOther ? L"Actualizar el parche" : L"Aplicar el parche");
    EnableWindow(GetDlgItem(dialog, IDC_APPLY), canApply);
    EnableWindow(GetDlgItem(dialog, IDC_REMOVE), canRemove);
}

INT_PTR CALLBACK MainDialog(HWND dialog, UINT message, WPARAM wParam, LPARAM)
{
    switch (message)
    {
    case WM_INITDIALOG:
        // Set here, not in patcher.rc: the resource compiler cuts control texts at 256 characters.
        SetDlgItemTextW(dialog, IDC_DESCRIPTION,
                        L"El parche hace que el juego funcione en Windows 10 y 11: un componente gráfico nuevo (Direct3D 11) a "
                        L"pantalla completa sin cambiar la resolución del escritorio, y las correcciones de memoria que evitan "
                        L"los nombres corruptos y los cuelgues. Hace copia de seguridad de lo que cambia, y se puede quitar.");
        Refresh(dialog);
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_BROWSE:
            if (auto folder = PickFolder(dialog); !folder.empty())
            {
                g_folder = folder;
                SetDlgItemTextW(dialog, IDC_RESULT, L"");
                Refresh(dialog);
            }
            return TRUE;
        case IDC_APPLY:
        case IDC_REMOVE:
        {
            const bool apply = LOWORD(wParam) == IDC_APPLY;
            HCURSOR previous = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
            std::wstring error;
            const bool ok = apply ? ApplyPatch(g_folder, error) : RemovePatch(g_folder, error);
            SetCursor(previous);
            std::wstring result;
            if (ok && apply)
                result = L"Parche aplicado. Ya puedes jugar con el acceso directo de siempre (el juego necesita el CD o su imagen .iso montada).";
            else if (ok)
                result = L"Parche quitado: el juego ha vuelto a su estado original.";
            else
                result = L"No se ha podido " + std::wstring(apply ? L"aplicar" : L"quitar") + L" el parche. " + error;
            SetDlgItemTextW(dialog, IDC_RESULT, result.c_str());
            Refresh(dialog);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(dialog, 0);
            return TRUE;
        }
        break;
    }
    return FALSE;
}
}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX controls{sizeof controls, ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);

    int count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    std::wstring command;
    for (int i = 1; i < count; ++i)
    {
        if (arguments[i][0] == L'/')
            command = arguments[i];
        else
            g_folder = arguments[i];
    }
    LocalFree(arguments);
    if (g_folder.empty())
        g_folder = ReadGameFolderKey();

    int result = 0;
    if (_wcsicmp(command.c_str(), L"/aplicar") == 0 || _wcsicmp(command.c_str(), L"/quitar") == 0)
    {
        std::wstring error;
        const bool apply = _wcsicmp(command.c_str(), L"/aplicar") == 0;
        result = (apply ? ApplyPatch(g_folder, error) : RemovePatch(g_folder, error)) ? 0 : 1;
    }
    else
    {
        DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_MAIN), nullptr, MainDialog, 0);
    }
    CoUninitialize();
    return result;
}
