// Optional changes to how the game plays, installed one by one with pcf2001-mods.exe on top of the patch (see
// docs/MODS.md). Each one changes bytes of a game file as this version of the patch leaves it, and can be removed by
// writing those bytes back.
#pragma once

#include "patch.h"

#include <filesystem>
#include <string>
#include <vector>

struct Mod
{
    const wchar_t* id;           // for the command line and the log
    const wchar_t* name;         // for the player
    const wchar_t* description;  // for the player
    const wchar_t* file;         // the game file it changes
    std::vector<ByteChange> changes;  // "original": as the patch leaves the file
};

const std::vector<Mod>& Mods();

// Undoes every mod installed in a game file's contents. False if a mod's bytes are damaged.
bool RemoveInstalledMods(const wchar_t* fileName, std::vector<uint8_t>& bytes);

struct ModsStatus
{
    bool ready = false;            // the game has this version of the patch, so mods can be installed and removed
    std::wstring details;          // for the player
    std::vector<bool> installed;   // one per mod, in the order of Mods()
};

ModsStatus CheckMods(const std::filesystem::path& gameFolder);

// Leaves installed exactly the mods marked in "wanted" (one per mod, in the order of Mods()). Returns false and sets
// an error text for the player on failure; the game's files are then as they were.
bool SetMods(const std::filesystem::path& gameFolder, const std::vector<bool>& wanted, std::wstring& error);
