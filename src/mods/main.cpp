// pcf2001-mods.exe: installs and removes optional mods, one by one, on top of the patch (see docs/MODS.md).
//
//   pcf2001-mods.exe                                 the window
//   pcf2001-mods.exe /instalar <mod>... [<carpeta>]  installs those mods without asking anything; exit code 0 on success
//   pcf2001-mods.exe /quitar <mod>... [<carpeta>]    removes them the same way
// Mods are named by their id (src/patcher/mods.cpp). Without a folder, the game is found through the registry value
// its installer wrote.
#include "mods.h"
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
bool g_filling = false;  // while Refresh sets the check boxes, their changes aren't the player's

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

void ShowModDescription(HWND dialog)
{
    const int selected = ListView_GetNextItem(GetDlgItem(dialog, IDC_LIST), -1, LVNI_SELECTED);
    SetDlgItemTextW(dialog, IDC_MOD_DESCRIPTION, selected >= 0 ? Mods()[selected].description : L"");
}

void Refresh(HWND dialog)
{
    SetDlgItemTextW(dialog, IDC_FOLDER, g_folder.c_str());
    const ModsStatus status = CheckMods(g_folder);
    SetDlgItemTextW(dialog, IDC_STATE, status.details.c_str());
    HWND list = GetDlgItem(dialog, IDC_LIST);
    g_filling = true;
    for (size_t i = 0; i < Mods().size(); ++i)
        ListView_SetCheckState(list, static_cast<int>(i), status.ready && status.installed[i]);
    g_filling = false;
    EnableWindow(list, status.ready);
    EnableWindow(GetDlgItem(dialog, IDC_APPLY), status.ready);
}

void FillList(HWND dialog)
{
    HWND list = GetDlgItem(dialog, IDC_LIST);
    ListView_SetExtendedListViewStyle(list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
    RECT client;
    GetClientRect(list, &client);
    LVCOLUMNW column{LVCF_WIDTH};
    column.cx = client.right - GetSystemMetrics(SM_CXVSCROLL);
    ListView_InsertColumn(list, 0, &column);
    for (size_t i = 0; i < Mods().size(); ++i)
    {
        LVITEMW item{LVIF_TEXT};
        item.iItem = static_cast<int>(i);
        item.pszText = const_cast<wchar_t*>(Mods()[i].name);
        ListView_InsertItem(list, &item);
    }
    if (!Mods().empty())
        ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}

INT_PTR CALLBACK MainDialog(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
        // Set here, not in mods.rc: the resource compiler cuts control texts at 256 characters.
        SetDlgItemTextW(dialog, IDC_DESCRIPTION,
                        L"Los mods cambian cómo se juega. Son opcionales: marca los que quieras, desmarca los que quieras "
                        L"quitar y aplica los cambios. Se instalan sobre el parche, que tiene que estar aplicado.");
        FillList(dialog);
        Refresh(dialog);
        ShowModDescription(dialog);
        return TRUE;
    case WM_NOTIFY:
    {
        const auto* header = reinterpret_cast<const NMHDR*>(lParam);
        if (header->idFrom == IDC_LIST && header->code == LVN_ITEMCHANGED && !g_filling)
        {
            const auto* change = reinterpret_cast<const NMLISTVIEW*>(lParam);
            if ((change->uChanged & LVIF_STATE) && ((change->uNewState ^ change->uOldState) & LVIS_SELECTED))
                ShowModDescription(dialog);
        }
        break;
    }
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
        {
            HWND list = GetDlgItem(dialog, IDC_LIST);
            std::vector<bool> wanted;
            for (size_t i = 0; i < Mods().size(); ++i)
                wanted.push_back(ListView_GetCheckState(list, static_cast<int>(i)) != 0);
            HCURSOR previous = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
            std::wstring error;
            const bool ok = SetMods(g_folder, wanted, error);
            SetCursor(previous);
            SetDlgItemTextW(dialog, IDC_RESULT, ok ? L"Cambios aplicados. Ya puedes jugar con el acceso directo de siempre."
                                                   : (L"No se han podido aplicar los cambios. " + error).c_str());
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
    INITCOMMONCONTROLSEX controls{sizeof controls, ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&controls);

    // Each argument is the command, a mod's id, or the folder.
    int count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    std::wstring command;
    std::vector<size_t> named;
    bool unknownMod = false;
    for (int i = 1; i < count; ++i)
    {
        if (arguments[i][0] == L'/')
        {
            command = arguments[i];
            continue;
        }
        bool isMod = false;
        for (size_t m = 0; m < Mods().size(); ++m)
        {
            if (_wcsicmp(arguments[i], Mods()[m].id) == 0)
            {
                named.push_back(m);
                isMod = true;
            }
        }
        if (!isMod && g_folder.empty())
            g_folder = arguments[i];
        else if (!isMod)
            unknownMod = true;  // a second folder: most likely a mistyped mod
    }
    LocalFree(arguments);
    if (g_folder.empty())
        g_folder = ReadGameFolderKey();

    int result = 0;
    const bool install = _wcsicmp(command.c_str(), L"/instalar") == 0;
    if (install || _wcsicmp(command.c_str(), L"/quitar") == 0)
    {
        // The other mods stay as they are.
        const ModsStatus status = CheckMods(g_folder);
        std::wstring error;
        result = 1;
        if (status.ready && !named.empty() && !unknownMod)
        {
            std::vector<bool> wanted = status.installed;
            for (size_t m : named)
                wanted[m] = install;
            result = SetMods(g_folder, wanted, error) ? 0 : 1;
        }
    }
    else
    {
        DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_MAIN), nullptr, MainDialog, 0);
    }
    CoUninitialize();
    return result;
}
