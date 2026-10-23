// The installer's log, pcf2001-setup.log in the game folder (or the temporary folder until the game folder exists),
// so players can send it when something goes wrong. Like the patch's log, it never contains full paths outside the
// game folder: those usually include the Windows user name.
#pragma once

#include <filesystem>
#include <string>

void OpenSetupLog(const std::filesystem::path& path);
void SetupLog(const std::wstring& line);

// A path as it may appear in the log: relative to the game folder when inside it, otherwise just the last part.
std::wstring LoggablePath(const std::filesystem::path& path, const std::filesystem::path& gameFolder);
