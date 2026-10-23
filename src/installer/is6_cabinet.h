// Reader for InstallShield 6 archives: a header file (DATA1.HDR) describing the files, and one or more volumes
// (DATA1.CAB, DATA2.CAB, ...) holding their data, compressed with DEFLATE in chunks. The format is described in
// docs/GAME-INTERNALS.md ("The CD's installer").
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

class Is6Cabinet
{
public:
    struct File
    {
        std::string name;       // file name, as stored (Windows-1252)
        std::string directory;  // directory inside the archive, possibly empty
        uint32_t flags = 0;
        uint64_t expandedSize = 0;
        uint64_t compressedSize = 0;
        uint64_t dataOffset = 0;
        uint8_t md5[16] = {};
        uint16_t volume = 0;     // number of the volume where the data starts (1 = DATA1.CAB)
        bool IsValid() const { return !(flags & kInvalid) && !name.empty(); }
        // Compressed files are inside the archive's volumes. The others are plain files on the CD, outside the
        // archive (e.g. GRAFICOS.PAK, the DBDAT folder); see LocateOnMedia.
        bool InArchive() const { return (flags & kCompressed) != 0 || expandedSize == 0; }
    };

    struct FileGroup
    {
        std::string name;
        uint32_t firstFile = 0;
        uint32_t lastFile = 0;  // inclusive
    };

    struct Component
    {
        std::string name;
        std::vector<std::string> fileGroups;
    };

    // Opens the archive whose header is `headerPath` (e.g. <CD>\DATA1.HDR). The volumes are expected next to it.
    // Returns false and sets the error text on failure.
    bool Open(const std::filesystem::path& headerPath);

    const std::vector<File>& Files() const { return m_files; }
    const std::vector<FileGroup>& FileGroups() const { return m_fileGroups; }
    const std::vector<Component>& Components() const { return m_components; }
    const std::string& Error() const { return m_error; }

    // Unpacks file `index` (one that is InArchive) and checks its MD5. Returns false and sets the error text on failure.
    bool Extract(size_t index, std::vector<uint8_t>& data);

    // Finds file `index` (one that isn't InArchive) on the CD, under `mediaRoot`: the first file with the same name
    // and size whose MD5 matches. Its folder on the CD doesn't always match the archive's directory (e.g. the MMX
    // version of Utils.dll is in MMX\). Returns false and sets the error text if there is none.
    bool LocateOnMedia(size_t index, const std::filesystem::path& mediaRoot, std::filesystem::path& found);

    // Files on the CD with the same name and size as file `index`, most likely first (the one in the archive's
    // directory). Cheap: nothing is read; whoever copies one checks its MD5.
    std::vector<std::filesystem::path> CandidatesOnMedia(size_t index, const std::filesystem::path& mediaRoot);

    // MD5 of a file on disk, in the same form as File::md5. Returns false if it can't be read.
    static bool FileMd5(const std::filesystem::path& path, uint8_t md5[16]);

    static constexpr uint32_t kSplit = 1;
    static constexpr uint32_t kObfuscated = 2;
    static constexpr uint32_t kCompressed = 4;
    static constexpr uint32_t kInvalid = 8;

private:
    struct Volume
    {
        uint32_t dataOffset = 0;
        uint32_t firstFile = 0;
        uint32_t lastFile = 0;
        uint64_t firstFileOffset = 0;
        uint64_t firstFileExpanded = 0;
        uint64_t firstFileCompressed = 0;
        uint64_t lastFileOffset = 0;
        uint64_t lastFileExpanded = 0;
        uint64_t lastFileCompressed = 0;
        std::vector<uint8_t> bytes;
        bool loaded = false;
    };

    bool Fail(std::string text);
    std::string String(uint32_t offset) const;   // a string at an offset from the cabinet descriptor
    uint32_t U32(size_t position) const;
    uint16_t U16(size_t position) const;
    uint64_t U64(size_t position) const;
    Volume* LoadVolume(uint16_t number);
    bool ReadRaw(const File& file, size_t index, std::vector<uint8_t>& raw);

    std::filesystem::path m_folder;
    std::string m_prefix;  // "DATA" for DATA1.HDR
    std::vector<uint8_t> m_header;
    size_t m_descriptor = 0;
    std::vector<File> m_files;
    std::vector<FileGroup> m_fileGroups;
    std::vector<Component> m_components;
    std::vector<Volume> m_volumes;  // index = volume number
    std::vector<std::filesystem::path> m_mediaFiles;  // every file on the CD, listed on first use
    std::filesystem::path m_mediaRoot;
    std::string m_error;
};
