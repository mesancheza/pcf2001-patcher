// pcf2001-setup.exe: installs PC FÚTBOL 2001 from the player's own CD (see docs/INSTALLER.md).
//
//   pcf2001-setup.exe                       the installation wizard
//   pcf2001-setup.exe /desatendido /cd=<unidad> /destino=<carpeta> /actualizaciones=<carpeta> [/sinescritorio]
//                                           installs without asking anything; exit code 0 when installed
//   pcf2001-setup.exe /desinstalar [<carpeta>] [/silencioso]
//                                           uninstalls (the copy in the game folder is registered for this)
#include "install_job.h"
#include "setup_log.h"
#include "text.h"
#include "wizard.h"

#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

#include <string>
#include <vector>

namespace
{
constexpr const wchar_t* kTitle = L"PC FÚTBOL 2001";

struct Arguments
{
    std::vector<std::wstring> words;  // arguments that aren't options

    bool Has(const wchar_t* option) const { return Value(option, nullptr); }

    // "/name" or "/name=value", case-insensitive.
    bool Value(const wchar_t* option, std::wstring* value) const
    {
        const size_t length = wcslen(option);
        for (const auto& argument : all)
        {
            if (_wcsnicmp(argument.c_str(), option, length) != 0)
                continue;
            if (argument.size() == length)
                return true;
            if (argument[length] == L'=')
            {
                if (value)
                    *value = argument.substr(length + 1);
                return true;
            }
        }
        return false;
    }

    std::vector<std::wstring> all;
};

Arguments ParseArguments()
{
    Arguments arguments;
    int count = 0;
    LPWSTR* list = CommandLineToArgvW(GetCommandLineW(), &count);
    for (int i = 1; i < count; ++i)
    {
        arguments.all.emplace_back(list[i]);
        if (list[i][0] != L'/')
            arguments.words.emplace_back(list[i]);
    }
    LocalFree(list);
    return arguments;
}

std::filesystem::path ExecutablePath()
{
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return path;
}

int Unattended(const Arguments& arguments)
{
    InstallOptions options;
    std::wstring value;
    if (arguments.Value(L"/cd", &value))
        options.cd = value;
    if (arguments.Value(L"/destino", &value))
        options.target = value;
    if (arguments.Value(L"/actualizaciones", &value))
        options.updates = value;
    options.desktopShortcut = !arguments.Has(L"/sinescritorio");
    std::atomic<bool> cancel{false};
    std::wstring error;
    const bool ok = RunInstall(options, [](const std::wstring&, double) {}, cancel, error);
    if (!ok)
        SetupLog(L"unattended installation failed: " + error);
    return ok ? 0 : 1;
}

int Uninstall(const Arguments& arguments)
{
    const bool silent = arguments.Has(L"/silencioso");
    const std::filesystem::path self = ExecutablePath();
    const std::filesystem::path folder = arguments.words.empty() ? self.parent_path() : std::filesystem::path(arguments.words[0]);

    // The copy in the game folder can't delete itself: run a copy from the temporary folder, which waits for this
    // process to end.
    std::wstring waitFor;
    if (!arguments.Value(L"/esperar", &waitFor))
    {
        wchar_t temp[MAX_PATH];
        GetTempPathW(MAX_PATH, temp);
        const auto copy = std::filesystem::path(temp) / L"pcf2001-setup-desinstalar.exe";
        if (!CopyFileW(self.c_str(), copy.c_str(), FALSE))
            return 1;
        std::wstring commandLine = L"\"" + copy.native() + L"\" /desinstalar \"" + folder.native() + L"\" /esperar=" +
                                   std::to_wstring(GetCurrentProcessId()) + (silent ? L" /silencioso" : L"");
        STARTUPINFOW startup{sizeof startup};
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(copy.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, temp, &startup, &process))
            return 1;
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return 0;
    }
    if (HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, std::stoul(waitFor)))
    {
        WaitForSingleObject(parent, 30000);
        CloseHandle(parent);
    }
    // This temporary copy goes away with the next restart.
    MoveFileExW(self.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);

    if (!silent && MessageBoxW(nullptr,
                               (L"¿Desinstalar PC FÚTBOL 2001 de " + folder.native() +
                                L"?\n\nLas partidas guardadas no se borran: se quedan en la carpeta Save.").c_str(),
                               kTitle, MB_ICONQUESTION | MB_YESNO) != IDYES)
        return 1;
    std::wstring error;
    const bool ok = RunUninstall(folder, error);
    if (!silent)
    {
        MessageBoxW(nullptr,
                    ok ? L"PC FÚTBOL 2001 se ha desinstalado. Las partidas guardadas siguen en la carpeta Save del juego, si había."
                       : error.c_str(),
                    kTitle, ok ? MB_ICONINFORMATION | MB_OK : MB_ICONERROR | MB_OK);
    }
    return ok ? 0 : 1;
}
}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX controls{sizeof controls, ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);

    const Arguments arguments = ParseArguments();
    int result;
    if (arguments.Has(L"/desinstalar"))
        result = Uninstall(arguments);
    else if (arguments.Has(L"/desatendido"))
        result = Unattended(arguments);
    else
        result = RunWizard(instance);
    CoUninitialize();
    return result;
}
