// The whole installation, without any user interface: used by the wizard (on a worker thread) and by the
// unattended mode. See docs/INSTALLER.md for the steps.
#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>

struct InstallOptions
{
    std::filesystem::path cd;               // root of the game CD (or a copy of it)
    std::filesystem::path target;           // game folder to create
    std::filesystem::path updates;          // folder with Dinamic's updates
    bool desktopShortcut = true;
};

// Progress: what is being done (for the player) and how far along the whole installation is, from 0 to 1.
using InstallProgress = std::function<void(const std::wstring& stage, double fraction)>;

// Checks the options before anything is written. Each returns an error text for the player, or an empty text.
std::wstring ValidateCd(const InstallOptions& options);
std::wstring ValidateTarget(const InstallOptions& options);  // folder, location and free space (needs the CD)
std::wstring ValidateUpdates(const InstallOptions& options);
std::wstring ValidateInstall(const InstallOptions& options);  // all of the above

// Installs. On failure everything installed so far is removed again, except the log. `cancel` can be set from
// another thread. Returns false and sets an error text for the player.
bool RunInstall(const InstallOptions& options, const InstallProgress& progress, const std::atomic<bool>& cancel, std::wstring& error);

// Space the installation needs, in bytes (the CD files plus room for the updates).
unsigned long long RequiredSpace(const std::filesystem::path& cd);

// Uninstalls the game in `gameFolder`: removes what the installer installed and registered. Saved games (the Save
// folder) are always kept. Returns false and sets an error text for the player.
bool RunUninstall(const std::filesystem::path& gameFolder, std::wstring& error);

// Name of the file listing everything the installer put in the game folder (used by the uninstaller).
constexpr const wchar_t* kInstalledFilesList = L"pcf2001-setup.files";
constexpr const wchar_t* kSetupLogName = L"pcf2001-setup.log";
constexpr const wchar_t* kUninstallerName = L"pcf2001-setup.exe";
