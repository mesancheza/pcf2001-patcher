// What Dinamic's setup installed from the CD, and the code that installs it (see docs/INSTALLER.md).
#pragma once

#include "is6_cabinet.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

// True if `root` is the game CD (or a copy of it): DISK.ID says "PCF9" and the installer's archive is there.
bool IsGameCd(const std::filesystem::path& root);
// Drives with the game CD in them, including mounted .iso images.
std::vector<std::filesystem::path> FindGameCds();

class GameFiles
{
public:
    struct Item
    {
        size_t index;                      // in the archive's file table
        std::filesystem::path destination; // relative to the game folder
        uint64_t size;
    };

    // Reads the CD's archive and decides what to install. Returns false and sets the error text on failure.
    bool Prepare(const std::filesystem::path& cdRoot, std::wstring& error);

    const std::vector<Item>& Items() const { return m_items; }
    uint64_t TotalBytes() const;

    // Copies everything into `target`, checking every file against the MD5 the archive stores. `progress` gets the
    // bytes done so far and returns false to cancel. `written` receives each installed file's path.
    bool Install(const std::filesystem::path& target, const std::function<bool(uint64_t)>& progress,
                 std::vector<std::filesystem::path>& written, std::wstring& error);

    // Empty folders Dinamic's setup created (for goals, notes and saved games).
    static std::vector<std::filesystem::path> EmptyFolders();

private:
    bool CopyFromCd(const Item& item, const std::filesystem::path& destination, uint64_t& done,
                    const std::function<bool(uint64_t)>& progress, std::wstring& error);

    Is6Cabinet m_cabinet;
    std::filesystem::path m_cdRoot;
    std::vector<Item> m_items;
};
