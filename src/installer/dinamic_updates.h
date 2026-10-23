// Dinamic's official updates, supplied by the player: the 3.6 update (PCFPATCH3.6.EXE) and the in-season data
// updates PCFACT01-12. Each one is an InstallShield "PackageForTheWeb" self-extractor holding a Microsoft cabinet with
// Dinamic's updater (PCFPATCH.exe), the RTPatch engine and the patch data. The installer unpacks the cabinet itself,
// runs the updater, and answers its dialogs. See docs/INSTALLER.md.
#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

struct DinamicUpdate
{
    const wchar_t* fileName;
    const wchar_t* description;  // for the installer's window and log
    unsigned long long size;
    const char* sha256;
    std::filesystem::path path;  // where the player has it; empty if not found
};

// The updates, in the order they must be applied (the order Dinamic released them).
std::vector<DinamicUpdate> RequiredUpdates();

// Looks for the updates in `folder`, by name, and checks each file found. Returns the error text for the first
// file that is missing or isn't the original, or an empty text when all are there.
std::wstring FindUpdates(const std::filesystem::path& folder, std::vector<DinamicUpdate>& updates);

// Applies one update to the game in `gameFolder`. The registry value Dinamic's updaters read (see
// system_integration.h) must already point there. `workFolder` is an empty folder for the unpacked updater.
bool ApplyUpdate(const DinamicUpdate& update, const std::filesystem::path& workFolder, std::wstring& error);
