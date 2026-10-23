// What the patch changes in an installed PC FÚTBOL 2001 v3.6, and the code that applies and removes it.
// See docs/PATCHER.md.
#pragma once

#include <filesystem>
#include <string>

enum class PatchState
{
    NotAGame,       // no PC FÚTBOL 2001 in that folder
    UnknownFiles,   // the game's files aren't the v3.6 ones the patch knows (another version, or modified)
    Unpatched,      // v3.6, without the patch
    Patched,        // v3.6 with this version of the patch
    PatchedOther,   // v3.6 with another version of the patch (applying again updates it)
};

struct PatchStatus
{
    PatchState state = PatchState::NotAGame;
    std::wstring details;  // for the player: what was found, or which file doesn't match
};

PatchStatus CheckGame(const std::filesystem::path& gameFolder);

// True if the game is running from that folder (its files can't be changed then).
bool GameIsRunning(const std::filesystem::path& gameFolder);

// Each returns false and sets an error text for the player on failure. Applying again updates the patch.
bool ApplyPatch(const std::filesystem::path& gameFolder, std::wstring& error);
bool RemovePatch(const std::filesystem::path& gameFolder, std::wstring& error);

// Files and folders the patch adds to the game folder (also removed by the installer's uninstaller).
const wchar_t* const* PatchFileNames();  // null-terminated list
constexpr const wchar_t* kPatchFolder = L"pcf2001-patch";
