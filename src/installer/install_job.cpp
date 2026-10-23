#include "install_job.h"

#include "dinamic_updates.h"
#include "game_files.h"
#include "setup_log.h"
#include "system_integration.h"
#include "text.h"

#include <windows.h>
#include <objbase.h>

#include <fstream>
#include <set>

#ifndef SETUP_VERSION
#define SETUP_VERSION "unknown"
#endif

namespace
{
// Share of the progress bar for each part: the CD files, then the 13 updates.
constexpr double kFilesShare = 0.6;
constexpr double kUpdatesShare = 0.37;
// Room for what the updates add, on top of the CD files.
constexpr unsigned long long kUpdatesSpace = 64ull * 1024 * 1024;

// Files the game itself writes into its folder (settings), removed by the uninstaller like the installed ones.
// Tactics the player saves (Tactics\tactic.000, ...) are kept, like saved games.
const wchar_t* const kGameSettingsFiles[] = {L"manager.ini", L"MainOpt.dat", L"sound.ini", L"sip.ini", L"update.ini"};

// What the patcher adds (src/patcher/patch.cpp), removed too when the game is uninstalled while patched.
const wchar_t* const kPatchFiles[] = {L"dcogl.dll", L"dcogl.ini", L"dcogl.log", L"dcogl.old.log"};
constexpr const wchar_t* kPatchFolder = L"pcf2001-patch";

std::filesystem::path ExecutablePath()
{
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return path;
}

bool IsSaveFolder(const std::filesystem::path& relative)
{
    return !relative.empty() && _wcsicmp(relative.begin()->c_str(), L"Save") == 0;
}

void LogHeader(const InstallOptions& options)
{
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    OSVERSIONINFOW version{sizeof version};
    if (auto get = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion")))
        get(&version);
    SetupLog(Format(L"pcf2001-setup %hs, Windows %lu.%lu.%lu", SETUP_VERSION, version.dwMajorVersion, version.dwMinorVersion,
                    version.dwBuildNumber));
    SetupLog(L"game folder: " + options.target.native());
    SetupLog(Format(L"desktop shortcut: %s", options.desktopShortcut ? L"yes" : L"no"));
}

// Everything in the game folder after the installation, so the uninstaller knows what it may remove.
bool WriteInstalledFilesList(const std::filesystem::path& target)
{
    std::ofstream out(target / kInstalledFilesList, std::ios::binary | std::ios::trunc);
    std::error_code error;
    for (auto it = std::filesystem::recursive_directory_iterator(target, error); it != std::filesystem::recursive_directory_iterator();
         it.increment(error))
    {
        if (error)
            break;
        if (!it->is_regular_file(error))
            continue;
        const auto relative = std::filesystem::relative(it->path(), target, error);
        if (relative == kInstalledFilesList || relative == kSetupLogName)
            continue;
        out << ToUtf8(relative.native()) << "\r\n";
    }
    return static_cast<bool>(out);
}

// Removes everything in a failed installation's folder except the log.
void RollBack(const std::filesystem::path& target)
{
    SetupLog(L"installation failed; removing what was installed");
    DeleteShortcuts();
    UnregisterUninstaller();
    DeleteGameFolderKey();
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(target, error))
    {
        if (entry.path().filename() != kSetupLogName)
            std::filesystem::remove_all(entry.path(), error);
    }
}
}  // namespace

unsigned long long RequiredSpace(const std::filesystem::path& cd)
{
    GameFiles files;
    std::wstring error;
    if (!files.Prepare(cd, error))
        return 0;
    return files.TotalBytes() + kUpdatesSpace;
}

std::wstring ValidateCd(const InstallOptions& options)
{
    if (options.cd.empty() || !IsGameCd(options.cd))
        return L"No se encuentra el CD de PC FÚTBOL 2001. Inserta el CD, o monta su imagen .iso con un doble clic.";
    return {};
}

std::wstring ValidateUpdates(const InstallOptions& options)
{
    std::vector<DinamicUpdate> updates;
    return FindUpdates(options.updates, updates);
}

std::wstring ValidateInstall(const InstallOptions& options)
{
    for (auto check : {ValidateCd, ValidateTarget, ValidateUpdates})
    {
        if (auto problem = check(options); !problem.empty())
            return problem;
    }
    return {};
}

std::wstring ValidateTarget(const InstallOptions& options)
{
    if (options.target.empty() || !options.target.is_absolute())
        return L"Elige la carpeta donde instalar el juego.";
    std::error_code error;
    if (std::filesystem::exists(options.target, error) && !std::filesystem::is_empty(options.target, error))
        return L"La carpeta elegida ya existe y no está vacía. Elige una carpeta nueva o vacía.";
    // The game writes into its own folder; Windows protects Program Files from that.
    wchar_t programFiles[MAX_PATH] = {}, programFilesX86[MAX_PATH] = {};
    GetEnvironmentVariableW(L"ProgramFiles", programFiles, MAX_PATH);
    GetEnvironmentVariableW(L"ProgramFiles(x86)", programFilesX86, MAX_PATH);
    for (const wchar_t* protectedFolder : {programFiles, programFilesX86})
    {
        const size_t length = wcslen(protectedFolder);
        if (length && _wcsnicmp(options.target.c_str(), protectedFolder, length) == 0)
            return L"El juego guarda sus partidas y su configuración en su propia carpeta, y Windows no lo permite dentro de "
                   L"\"Archivos de programa\". Elige otra carpeta, por ejemplo C:\\Juegos\\PC Futbol 2001.";
    }
    const unsigned long long needed = RequiredSpace(options.cd);
    ULARGE_INTEGER available{};
    std::filesystem::path existing = options.target;
    while (!existing.empty() && !std::filesystem::exists(existing, error))
        existing = existing.parent_path();
    if (GetDiskFreeSpaceExW(existing.c_str(), &available, nullptr, nullptr) && available.QuadPart < needed)
        return Format(L"No hay espacio suficiente: hacen falta %llu MB.", needed / (1024 * 1024));
    return {};
}

bool RunInstall(const InstallOptions& options, const InstallProgress& progress, const std::atomic<bool>& cancel, std::wstring& error)
{
    if (auto problem = ValidateInstall(options); !problem.empty())
    {
        error = problem;
        return false;
    }
    std::error_code fsError;
    std::filesystem::create_directories(options.target, fsError);
    if (fsError)
    {
        error = L"No se puede crear la carpeta " + options.target.native();
        return false;
    }
    OpenSetupLog(options.target / kSetupLogName);
    LogHeader(options);

    auto fail = [&](const std::wstring& message) {
        error = message;
        SetupLog(L"ERROR: " + message);
        RollBack(options.target);
        return false;
    };

    // 1. The game's files from the CD.
    progress(L"Leyendo el CD...", 0.0);
    GameFiles files;
    if (!files.Prepare(options.cd, error))
        return fail(error);
    const double total = static_cast<double>(files.TotalBytes());
    std::vector<std::filesystem::path> written;
    const bool copied = files.Install(
        options.target,
        [&](uint64_t done) {
            progress(L"Copiando los archivos del juego...", kFilesShare * static_cast<double>(done) / total);
            return !cancel.load();
        },
        written, error);
    if (!copied)
        return fail(error);

    // 2. Write access for every user, and the registry value Dinamic's updaters read.
    progress(L"Preparando las actualizaciones...", kFilesShare);
    GrantUsersModify(options.target);
    if (!WriteGameFolderKey(options.target))
        return fail(L"No se puede escribir en el registro de Windows la carpeta del juego.");

    // 3. Dinamic's updates, in order.
    std::vector<DinamicUpdate> updates;
    if (auto problem = FindUpdates(options.updates, updates); !problem.empty())
        return fail(problem);
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    const auto work = std::filesystem::path(temp) / L"pcf2001-setup";
    for (size_t i = 0; i < updates.size(); ++i)
    {
        if (cancel.load())
            return fail(L"Instalación cancelada.");
        progress(Format(L"Aplicando: %s...", updates[i].description), kFilesShare + kUpdatesShare * static_cast<double>(i) / updates.size());
        const auto folder = work / std::to_wstring(i + 1);
        std::filesystem::remove_all(folder, fsError);
        const bool applied = ApplyUpdate(updates[i], folder, error);
        std::filesystem::remove_all(folder, fsError);
        if (!applied)
            return fail(error);
    }
    std::filesystem::remove_all(work, fsError);

    // 4. The uninstaller (a copy of this program), shortcuts and the entry in Windows' list of programs.
    progress(L"Creando los accesos directos...", kFilesShare + kUpdatesShare);
    const auto uninstaller = options.target / kUninstallerName;
    if (!CopyFileW(ExecutablePath().c_str(), uninstaller.c_str(), FALSE))
        return fail(L"No se puede copiar el desinstalador en la carpeta del juego.");
    if (!WriteInstalledFilesList(options.target))
        return fail(L"No se puede escribir la lista de archivos instalados.");
    CreateShortcuts(options.target, uninstaller, options.desktopShortcut);
    RegisterUninstaller(options.target, uninstaller, files.TotalBytes() + kUpdatesSpace);

    SetupLog(L"installation complete");
    progress(L"Instalación terminada.", 1.0);
    return true;
}

bool RunUninstall(const std::filesystem::path& gameFolder, std::wstring& error)
{
    std::ifstream list(gameFolder / kInstalledFilesList, std::ios::binary);
    if (!list)
    {
        // Not a game folder: leave no log in it.
        error = L"Esta carpeta no tiene una instalación hecha con este programa: " + gameFolder.native();
        return false;
    }
    OpenSetupLog(gameFolder / kSetupLogName);
    SetupLog(L"uninstalling");

    // The installed files, and the settings the game writes, but never anything under Save.
    std::vector<std::filesystem::path> remove;
    for (std::string line; std::getline(list, line);)
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (!line.empty())
            remove.emplace_back(FromUtf8(line));
    }
    list.close();
    for (const wchar_t* name : kGameSettingsFiles)
        remove.emplace_back(name);
    for (const wchar_t* name : kPatchFiles)
        remove.emplace_back(name);
    {
        std::error_code fsError;
        std::filesystem::remove_all(gameFolder / kPatchFolder, fsError);
    }
    remove.emplace_back(kInstalledFilesList);
    remove.emplace_back(kUninstallerName);

    std::set<std::filesystem::path> folders;
    size_t removed = 0;
    for (const auto& relative : remove)
    {
        if (IsSaveFolder(relative))
            continue;
        std::error_code fsError;
        const auto path = gameFolder / relative;
        SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);  // some files come read-only from the CD
        if (std::filesystem::remove(path, fsError))
            ++removed;
        for (auto parent = relative.parent_path(); !parent.empty(); parent = parent.parent_path())
            folders.insert(parent);
    }
    // Empty folders, deepest first (the game's empty Goles and Notas included).
    for (const auto& folder : GameFiles::EmptyFolders())
        folders.insert(folder);
    for (auto it = folders.rbegin(); it != folders.rend(); ++it)
    {
        std::error_code fsError;
        if (!IsSaveFolder(*it) && std::filesystem::is_empty(gameFolder / *it, fsError))
            std::filesystem::remove(gameFolder / *it, fsError);
    }

    DeleteShortcuts();
    UnregisterUninstaller();
    if (_wcsicmp(ReadGameFolderKey().c_str(), gameFolder.c_str()) == 0)
        DeleteGameFolderKey();
    SetupLog(Format(L"uninstalled: %zu files removed; saved games kept", removed));

    // The log goes last; the folder itself only if nothing is left (saved games stay).
    OpenSetupLog({});
    std::error_code fsError;
    std::filesystem::remove(gameFolder / kSetupLogName, fsError);
    if (std::filesystem::is_empty(gameFolder, fsError))
        std::filesystem::remove(gameFolder, fsError);
    return true;
}
