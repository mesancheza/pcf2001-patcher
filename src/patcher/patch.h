// What the patch changes in an installed PC FÚTBOL 2001 v3.6, and the code that applies and removes it.
// See docs/PATCHER.md.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Bytes at a file offset, as they are in v3.6 and as the patch leaves them. Mods (mods.h) use the same form, with
// "original" meaning the file as the patch leaves it.
struct ByteChange
{
    size_t offset;
    std::vector<uint8_t> original;
    std::vector<uint8_t> patched;
};

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

// SHA-256 of a game file as this version of the patch leaves it (the v3.6 hash for files it doesn't change), in
// lowercase hex; empty for a file the patch doesn't know.
std::string PatchedSha256(const wchar_t* fileName);

// File helpers, shared with the mods tool.
bool ReadBytes(const std::filesystem::path& path, std::vector<uint8_t>& bytes);
bool WriteBytes(const std::filesystem::path& path, const void* data, size_t size);  // all or nothing
std::string HexSha256(const void* data, size_t size);
std::string FileSha256(const std::filesystem::path& path);
