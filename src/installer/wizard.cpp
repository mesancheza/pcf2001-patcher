#include "wizard.h"

#include "dinamic_updates.h"
#include "game_files.h"
#include "install_job.h"
#include "resource.h"
#include "text.h"

#include <commctrl.h>
#include <prsht.h>
#include <shobjidl.h>

#include <atomic>
#include <thread>

namespace
{
constexpr UINT kProgressMessage = WM_APP + 1;  // the worker thread reports progress
constexpr UINT kDoneMessage = WM_APP + 2;      // the worker thread has finished

struct WizardState
{
    InstallOptions options;
    std::thread worker;
    std::atomic<bool> cancel{false};
    std::atomic<bool> running{false};
    bool succeeded = false;
    std::wstring error;
    HWND progressPage = nullptr;
    // The latest progress, passed from the worker thread to the window.
    CRITICAL_SECTION lock;
    std::wstring stage;
    double fraction = 0;
};

WizardState* g_state;

std::wstring ItemText(HWND dialog, int id)
{
    wchar_t text[MAX_PATH * 2];
    GetDlgItemTextW(dialog, id, text, static_cast<int>(std::size(text)));
    return text;
}

// The standard "choose a folder" dialog. Returns an empty path if cancelled.
std::filesystem::path PickFolder(HWND owner, const wchar_t* title)
{
    std::filesystem::path result;
    IFileOpenDialog* picker = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&picker))))
        return result;
    DWORD options = 0;
    picker->GetOptions(&options);
    picker->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    picker->SetTitle(title);
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

void RefuseNext(HWND page, const std::wstring& message)
{
    MessageBoxW(GetParent(page), message.c_str(), L"Instalar PC FÚTBOL 2001", MB_ICONWARNING | MB_OK);
    SetWindowLongPtrW(page, DWLP_MSGRESULT, -1);
}

// --- CD -------------------------------------------------------------------------------------------------------------

void FillCdList(HWND page, const std::filesystem::path& extra)
{
    HWND list = GetDlgItem(page, IDC_CD_LIST);
    SendMessageW(list, CB_RESETCONTENT, 0, 0);
    auto drives = FindGameCds();
    if (!extra.empty() && IsGameCd(extra))
        drives.insert(drives.begin(), extra);
    for (const auto& drive : drives)
        SendMessageW(list, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(drive.c_str()));
    SendMessageW(list, CB_SETCURSEL, 0, 0);
    SetDlgItemTextW(page, IDC_CD_STATUS,
                    drives.empty()
                        ? L"No se encuentra el CD de PC FÚTBOL 2001.\n\nInserta el CD en la unidad, o si tienes una imagen del CD "
                          L"(un archivo .iso), haz doble clic sobre ella para que Windows la monte como una unidad. Después "
                          L"pulsa \"Buscar de nuevo\".\n\nTambién puedes elegir una carpeta con una copia del contenido del CD."
                        : L"CD de PC FÚTBOL 2001 encontrado.");
}

INT_PTR CALLBACK SourcePage(HWND page, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
        FillCdList(page, {});
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_CD_REFRESH)
            FillCdList(page, {});
        else if (LOWORD(wParam) == IDC_CD_BROWSE)
        {
            const auto folder = PickFolder(page, L"Carpeta con el contenido del CD de PC FÚTBOL 2001");
            if (!folder.empty())
            {
                if (IsGameCd(folder))
                    FillCdList(page, folder);
                else
                    MessageBoxW(GetParent(page), L"Esa carpeta no contiene el CD de PC FÚTBOL 2001 (faltan DISK.ID o DATA1.HDR).",
                                L"Instalar PC FÚTBOL 2001", MB_ICONWARNING | MB_OK);
            }
        }
        return TRUE;
    case WM_NOTIFY:
        switch (reinterpret_cast<NMHDR*>(lParam)->code)
        {
        case PSN_SETACTIVE:
            PropSheet_SetWizButtons(GetParent(page), PSWIZB_BACK | PSWIZB_NEXT);
            return TRUE;
        case PSN_WIZNEXT:
        {
            const std::filesystem::path cd = ItemText(page, IDC_CD_LIST);
            if (cd.empty() || !IsGameCd(cd))
            {
                RefuseNext(page, L"Inserta el CD de PC FÚTBOL 2001 (o monta su imagen .iso) y pulsa \"Buscar de nuevo\".");
                return TRUE;
            }
            g_state->options.cd = cd;
            return TRUE;
        }
        }
        break;
    }
    return FALSE;
}

// --- Game folder ----------------------------------------------------------------------------------------------------

INT_PTR CALLBACK TargetPage(HWND page, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
        SetDlgItemTextW(page, IDC_TARGET_EDIT, L"C:\\Juegos\\PC Futbol 2001");
        CheckDlgButton(page, IDC_DESKTOP, BST_CHECKED);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_TARGET_BROWSE)
        {
            const auto folder = PickFolder(page, L"Carpeta donde instalar PC FÚTBOL 2001");
            if (!folder.empty())
                SetDlgItemTextW(page, IDC_TARGET_EDIT, (folder / L"PC Futbol 2001").c_str());
        }
        return TRUE;
    case WM_NOTIFY:
        switch (reinterpret_cast<NMHDR*>(lParam)->code)
        {
        case PSN_SETACTIVE:
            SetDlgItemTextW(page, IDC_TARGET_SPACE,
                            Format(L"Espacio necesario: %llu MB.", RequiredSpace(g_state->options.cd) / (1024 * 1024)).c_str());
            PropSheet_SetWizButtons(GetParent(page), PSWIZB_BACK | PSWIZB_NEXT);
            return TRUE;
        case PSN_WIZNEXT:
        {
            InstallOptions options = g_state->options;
            options.target = ItemText(page, IDC_TARGET_EDIT);
            if (const std::wstring problem = ValidateTarget(options); !problem.empty())
            {
                RefuseNext(page, problem);
                return TRUE;
            }
            g_state->options.target = options.target;
            g_state->options.desktopShortcut = IsDlgButtonChecked(page, IDC_DESKTOP) == BST_CHECKED;
            return TRUE;
        }
        }
        break;
    }
    return FALSE;
}

// --- Dinamic's updates ----------------------------------------------------------------------------------------------

void ShowUpdates(HWND page)
{
    const std::filesystem::path folder = ItemText(page, IDC_UPDATES_EDIT);
    std::wstring text = L"Hacen falta todas, en esta carpeta y con su nombre original:\n";
    for (auto update : RequiredUpdates())
    {
        std::error_code error;
        const bool present = !folder.empty() && std::filesystem::exists(folder / update.fileName, error);
        text += Format(L"\n    %s   %s", present ? L"✔" : L"✘", update.fileName);
    }
    std::vector<DinamicUpdate> updates;
    const std::wstring problem = folder.empty() ? std::wstring(L"Elige la carpeta.") : FindUpdates(folder, updates);
    text += L"\n\n" + (problem.empty() ? std::wstring(L"Todas las actualizaciones están listas.") : problem);
    SetDlgItemTextW(page, IDC_UPDATES_STATUS, text.c_str());
}

INT_PTR CALLBACK UpdatesPage(HWND page, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
        ShowUpdates(page);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_UPDATES_BROWSE)
        {
            const auto folder = PickFolder(page, L"Carpeta con PCFPATCH3.6.EXE y PCFACT01.exe a PCFACT12.exe");
            if (!folder.empty())
                SetDlgItemTextW(page, IDC_UPDATES_EDIT, folder.c_str());
        }
        else if (LOWORD(wParam) == IDC_UPDATES_EDIT && HIWORD(wParam) == EN_CHANGE)
            ShowUpdates(page);
        return TRUE;
    case WM_NOTIFY:
        switch (reinterpret_cast<NMHDR*>(lParam)->code)
        {
        case PSN_SETACTIVE:
            PropSheet_SetWizButtons(GetParent(page), PSWIZB_BACK | PSWIZB_NEXT);
            return TRUE;
        case PSN_WIZNEXT:
        {
            InstallOptions options = g_state->options;
            options.updates = ItemText(page, IDC_UPDATES_EDIT);
            if (const std::wstring problem = ValidateInstall(options); !problem.empty())
            {
                RefuseNext(page, problem);
                return TRUE;
            }
            g_state->options.updates = options.updates;
            return TRUE;
        }
        }
        break;
    }
    return FALSE;
}

// --- Progress -------------------------------------------------------------------------------------------------------

void StartInstall(HWND page)
{
    g_state->progressPage = page;
    g_state->running = true;
    g_state->worker = std::thread([] {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);  // shortcuts use COM
        std::wstring error;
        const bool ok = RunInstall(
            g_state->options,
            [](const std::wstring& stage, double fraction) {
                EnterCriticalSection(&g_state->lock);
                g_state->stage = stage;
                g_state->fraction = fraction;
                LeaveCriticalSection(&g_state->lock);
                PostMessageW(g_state->progressPage, kProgressMessage, 0, 0);
            },
            g_state->cancel, error);
        g_state->succeeded = ok;
        g_state->error = error;
        CoUninitialize();
        PostMessageW(g_state->progressPage, kDoneMessage, 0, 0);
    });
}

INT_PTR CALLBACK ProgressPage(HWND page, UINT message, WPARAM, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
        SendDlgItemMessageW(page, IDC_PROGRESS_BAR, PBM_SETRANGE32, 0, 1000);
        return TRUE;
    case kProgressMessage:
    {
        EnterCriticalSection(&g_state->lock);
        const std::wstring stage = g_state->stage;
        const double fraction = g_state->fraction;
        LeaveCriticalSection(&g_state->lock);
        SetDlgItemTextW(page, IDC_PROGRESS_TEXT, stage.c_str());
        SendDlgItemMessageW(page, IDC_PROGRESS_BAR, PBM_SETPOS, static_cast<WPARAM>(fraction * 1000), 0);
        return TRUE;
    }
    case kDoneMessage:
        g_state->worker.join();
        g_state->running = false;
        PropSheet_PressButton(GetParent(page), PSBTN_NEXT);
        return TRUE;
    case WM_NOTIFY:
        switch (reinterpret_cast<NMHDR*>(lParam)->code)
        {
        case PSN_SETACTIVE:
            PropSheet_SetWizButtons(GetParent(page), 0);
            if (!g_state->running && !g_state->worker.joinable())
                StartInstall(page);
            return TRUE;
        case PSN_WIZNEXT:
            if (g_state->running)
                SetWindowLongPtrW(page, DWLP_MSGRESULT, -1);  // not until the installation has finished
            return TRUE;
        case PSN_QUERYCANCEL:
            if (g_state->running)
            {
                if (MessageBoxW(GetParent(page), L"¿Cancelar la instalación? Se borrará lo instalado hasta ahora.",
                                L"Instalar PC FÚTBOL 2001", MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2) == IDYES)
                    g_state->cancel = true;
                SetWindowLongPtrW(page, DWLP_MSGRESULT, TRUE);  // the window closes when the worker has stopped
            }
            return TRUE;
        }
        break;
    }
    return FALSE;
}

// --- End ------------------------------------------------------------------------------------------------------------

INT_PTR CALLBACK FinishPage(HWND page, UINT message, WPARAM, LPARAM lParam)
{
    if (message == WM_NOTIFY && reinterpret_cast<NMHDR*>(lParam)->code == PSN_SETACTIVE)
    {
        std::wstring text;
        if (g_state->succeeded)
        {
            text = L"PC FÚTBOL 2001 se ha instalado en " + g_state->options.target.native() +
                   L", con la versión 3.6 y todas las actualizaciones de datos.\n\nPara jugar, usa el acceso directo "
                   L"\"PC FÚTBOL 2001\" del menú Inicio. El juego necesita el CD (o su imagen .iso montada).";
        }
        else
        {
            text = L"No se ha podido instalar el juego.\n\n" + g_state->error + L"\n\nLos detalles están en " +
                   (g_state->options.target / kSetupLogName).native() +
                   L". Si pides ayuda, adjunta ese archivo.";
        }
        SetDlgItemTextW(page, IDC_FINISH_TEXT, text.c_str());
        PropSheet_SetWizButtons(GetParent(page), PSWIZB_FINISH);
        PropSheet_CancelToClose(GetParent(page));
        return TRUE;
    }
    return FALSE;
}

INT_PTR CALLBACK WelcomePage(HWND page, UINT message, WPARAM, LPARAM lParam)
{
    if (message == WM_INITDIALOG)
    {
        // Set here, not in installer.rc: the resource compiler cuts control texts at 256 characters.
        SetDlgItemTextW(page, IDC_TEXT,
                        L"Este programa instala PC FÚTBOL 2001 desde tu propio CD del juego, y le aplica las actualizaciones "
                        L"oficiales de Dinamic: la versión 3.6 y las 12 actualizaciones de datos de la temporada.\n\n"
                        L"Necesitarás:\n\n"
                        L"  •  el CD del juego, o su imagen .iso montada (doble clic sobre ella);\n"
                        L"  •  los archivos de las actualizaciones: PCFPATCH3.6.EXE y PCFACT01.exe a PCFACT12.exe.\n\n"
                        L"El programa no incluye ningún archivo de Dinamic: solo usa los tuyos.");
        return TRUE;
    }
    if (message == WM_NOTIFY && reinterpret_cast<NMHDR*>(lParam)->code == PSN_SETACTIVE)
    {
        PropSheet_SetWizButtons(GetParent(page), PSWIZB_NEXT);
        return TRUE;
    }
    return FALSE;
}

PROPSHEETPAGEW Page(HINSTANCE instance, int id, DLGPROC procedure, const wchar_t* title)
{
    PROPSHEETPAGEW page{sizeof page};
    page.dwFlags = PSP_USEHEADERTITLE;
    page.hInstance = instance;
    page.pszTemplate = MAKEINTRESOURCEW(id);
    page.pfnDlgProc = procedure;
    page.pszHeaderTitle = title;
    return page;
}
}  // namespace

int RunWizard(HINSTANCE instance)
{
    WizardState state;
    InitializeCriticalSection(&state.lock);
    g_state = &state;

    PROPSHEETPAGEW pages[] = {
        Page(instance, IDD_WELCOME, WelcomePage, L"Bienvenido al instalador de PC FÚTBOL 2001"),
        Page(instance, IDD_SOURCE, SourcePage, L"El CD del juego"),
        Page(instance, IDD_TARGET, TargetPage, L"Dónde instalarlo"),
        Page(instance, IDD_UPDATES, UpdatesPage, L"Las actualizaciones de Dinamic"),
        Page(instance, IDD_PROGRESS, ProgressPage, L"Instalando"),
        Page(instance, IDD_FINISH, FinishPage, L"Fin"),
    };
    PROPSHEETHEADERW header{sizeof header};
    header.dwFlags = PSH_WIZARD | PSH_AEROWIZARD | PSH_PROPSHEETPAGE;
    header.hInstance = instance;
    header.pszCaption = L"Instalar PC FÚTBOL 2001";
    header.nPages = static_cast<UINT>(std::size(pages));
    header.ppsp = pages;
    PropertySheetW(&header);

    if (state.worker.joinable())
    {
        state.cancel = true;
        state.worker.join();
    }
    DeleteCriticalSection(&state.lock);
    g_state = nullptr;
    return state.succeeded ? 0 : 1;
}
