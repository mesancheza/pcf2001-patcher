// What the installer registers in Windows: the registry value Dinamic's setup wrote (and its updaters and community
// tools read), write access to the game folder for every user, shortcuts, and the entry in Windows' list of installed
// programs. All of it is undone by the uninstaller. Needs administrator rights.
#pragma once

#include <filesystem>
#include <string>

constexpr const wchar_t* kGameName = L"PC FÚTBOL 2001";

// HKLM\SOFTWARE\DINAMIC MULTIMEDIA\PC FÚTBOL 2001, value DIR (in the 32-bit registry view, as the game is 32-bit).
bool WriteGameFolderKey(const std::filesystem::path& gameFolder);
std::filesystem::path ReadGameFolderKey();
void DeleteGameFolderKey();

// The game writes its settings and saved games into its own folder, so every user needs write access to it.
bool GrantUsersModify(const std::filesystem::path& folder);

// Start menu folder "PC FÚTBOL 2001" with the game and the uninstaller, and optionally a desktop shortcut, for all
// users (as Dinamic's setup did).
bool CreateShortcuts(const std::filesystem::path& gameFolder, const std::filesystem::path& uninstaller, bool desktop);
void DeleteShortcuts();

// The entry in Settings > Apps and in Programs and Features.
bool RegisterUninstaller(const std::filesystem::path& gameFolder, const std::filesystem::path& uninstaller,
                         unsigned long long sizeBytes);
void UnregisterUninstaller();
