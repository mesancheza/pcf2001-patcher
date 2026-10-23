// Development tool: lists or extracts the contents of an InstallShield 6 archive, such as the game CD's DATA1.HDR.
//
//   is6-cab list <DATA1.HDR>
//   is6-cab manifest <DATA1.HDR>            (every file, with its MD5 and where it is: in the archive or on the CD)
//   is6-cab extract <DATA1.HDR> <folder>    (files inside the archive go to <folder>\<file group>\<directory>\<name>)
#include "is6_cabinet.h"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace
{
const Is6Cabinet::FileGroup* GroupOf(const Is6Cabinet& cabinet, size_t index)
{
    for (const auto& group : cabinet.FileGroups())
    {
        if (index >= group.firstFile && index <= group.lastFile)
            return &group;
    }
    return nullptr;
}

int List(Is6Cabinet& cabinet)
{
    for (const auto& component : cabinet.Components())
    {
        std::printf("component \"%s\":", component.name.c_str());
        for (const auto& group : component.fileGroups)
            std::printf(" [%s]", group.c_str());
        std::printf("\n");
    }
    for (const auto& group : cabinet.FileGroups())
        std::printf("file group \"%s\": files %u-%u\n", group.name.c_str(), group.firstFile, group.lastFile);
    const auto& files = cabinet.Files();
    for (size_t i = 0; i < files.size(); ++i)
    {
        const auto& f = files[i];
        if (!f.IsValid())
            continue;
        const auto* group = GroupOf(cabinet, i);
        std::printf("%4zu  %-24s %10llu  vol %u  flags %x  %s%s%s\n", i, group ? group->name.c_str() : "?",
                    static_cast<unsigned long long>(f.expandedSize), f.volume, f.flags, f.directory.c_str(),
                    f.directory.empty() ? "" : "\\", f.name.c_str());
    }
    return 0;
}

// Tab-separated: index, file group, directory\name, size, MD5, and where the data is: "archive" or the path on the CD.
int Manifest(Is6Cabinet& cabinet, const std::filesystem::path& mediaRoot)
{
    int missing = 0;
    const auto& files = cabinet.Files();
    for (size_t i = 0; i < files.size(); ++i)
    {
        const auto& f = files[i];
        if (!f.IsValid())
            continue;
        std::string source = "archive";
        if (!f.InArchive())
        {
            std::filesystem::path found;
            if (cabinet.LocateOnMedia(i, mediaRoot, found))
                source = std::filesystem::relative(found, mediaRoot).string();
            else
            {
                source = "MISSING";
                ++missing;
            }
        }
        char md5[33];
        for (int k = 0; k < 16; ++k)
            std::snprintf(md5 + 2 * k, 3, "%02x", f.md5[k]);
        const auto* group = GroupOf(cabinet, i);
        std::printf("%zu\t%s\t%s%s%s\t%llu\t%s\t%s\n", i, group ? group->name.c_str() : "?", f.directory.c_str(),
                    f.directory.empty() ? "" : "\\", f.name.c_str(), static_cast<unsigned long long>(f.expandedSize), md5,
                    source.c_str());
    }
    std::fprintf(stderr, "%d files not found on the CD\n", missing);
    return missing ? 1 : 0;
}

int Extract(Is6Cabinet& cabinet, const std::filesystem::path& folder)
{
    int failures = 0;
    std::vector<uint8_t> data;
    const auto& files = cabinet.Files();
    for (size_t i = 0; i < files.size(); ++i)
    {
        const auto& f = files[i];
        if (!f.IsValid() || !f.InArchive())
            continue;
        if (!cabinet.Extract(i, data))
        {
            std::printf("FAILED %zu %s: %s\n", i, f.name.c_str(), cabinet.Error().c_str());
            ++failures;
            continue;
        }
        const auto* group = GroupOf(cabinet, i);
        std::string groupFolder = group ? group->name : std::string("no group");
        for (char& c : groupFolder)
        {
            if (std::strchr("<>:\"/\\|?*", c))
                c = '_';  // group names such as "<Engine>Engine Files" aren't valid folder names
        }
        auto path = folder / groupFolder;
        if (!f.directory.empty())
            path /= f.directory;
        std::filesystem::create_directories(path);
        std::ofstream(path / f.name, std::ios::binary).write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }
    std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
}  // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf("usage: is6-cab list|manifest <DATA1.HDR> | extract <DATA1.HDR> <folder>\n");
        return 2;
    }
    Is6Cabinet cabinet;
    if (!cabinet.Open(argv[2]))
    {
        std::printf("error: %s\n", cabinet.Error().c_str());
        return 1;
    }
    const std::string command = argv[1];
    if (command == "list")
        return List(cabinet);
    if (command == "manifest")
        return Manifest(cabinet, std::filesystem::path(argv[2]).parent_path());
    if (command == "extract" && argc >= 4)
        return Extract(cabinet, argv[3]);
    std::printf("unknown command\n");
    return 2;
}
