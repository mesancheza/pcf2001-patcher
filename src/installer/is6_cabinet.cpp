#include "is6_cabinet.h"

#include "inflate.h"
#include "hashes.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace
{
constexpr uint32_t kSignature = 0x28635349;  // "ISc("
constexpr size_t kCommonHeaderSize = 0x14;
constexpr int kMaxFileGroups = 71;
constexpr int kMaxComponents = 71;
constexpr size_t kFileDescriptorSize = 0x57;

bool ReadWholeFile(const std::filesystem::path& path, std::vector<uint8_t>& bytes)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    in.seekg(0, std::ios::end);
    bytes.resize(static_cast<size_t>(in.tellg()));
    in.seekg(0);
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(in);
}

uint32_t Read32(const std::vector<uint8_t>& bytes, size_t position)
{
    if (position + 4 > bytes.size())
        return 0;
    uint32_t value;
    std::memcpy(&value, bytes.data() + position, 4);
    return value;
}

// InstallShield's obfuscation of stored data, reversed. The seed is the position within the file's data.
void Deobfuscate(uint8_t* data, size_t size, uint32_t& seed)
{
    for (size_t i = 0; i < size; ++i, ++seed)
    {
        const uint8_t x = data[i] ^ 0xD5;
        data[i] = static_cast<uint8_t>(((x >> 2) | (x << 6)) - (seed % 0x47));
    }
}
}  // namespace

bool Is6Cabinet::Fail(std::string text)
{
    m_error = std::move(text);
    return false;
}

uint32_t Is6Cabinet::U32(size_t position) const
{
    return Read32(m_header, position);
}

uint16_t Is6Cabinet::U16(size_t position) const
{
    if (position + 2 > m_header.size())
        return 0;
    return static_cast<uint16_t>(m_header[position] | (m_header[position + 1] << 8));
}

uint64_t Is6Cabinet::U64(size_t position) const
{
    return U32(position) | (static_cast<uint64_t>(U32(position + 4)) << 32);
}

std::string Is6Cabinet::String(uint32_t offset) const
{
    const size_t start = m_descriptor + offset;
    if (offset == 0 || start >= m_header.size())
        return {};
    const auto* begin = reinterpret_cast<const char*>(m_header.data() + start);
    return std::string(begin, strnlen(begin, m_header.size() - start));
}

bool Is6Cabinet::Open(const std::filesystem::path& headerPath)
{
    m_folder = headerPath.parent_path();
    std::string stem = headerPath.stem().string();
    while (!stem.empty() && isdigit(static_cast<unsigned char>(stem.back())))
        stem.pop_back();
    m_prefix = stem;
    if (!ReadWholeFile(headerPath, m_header))
        return Fail("cannot read " + headerPath.string());
    if (U32(0) != kSignature)
        return Fail(headerPath.string() + " is not an InstallShield archive");
    const uint32_t version = U32(4);
    const uint32_t major = (version >> 24) == 1 ? (version >> 12) & 0xF : 0;
    if (major != 6)
        return Fail("unsupported InstallShield archive version");
    m_descriptor = U32(0x0C);
    if (m_descriptor + 0x15A + 4 * kMaxComponents > m_header.size())
        return Fail("truncated InstallShield header");

    // Cabinet descriptor
    const size_t d = m_descriptor;
    const uint32_t fileTableOffset = U32(d + 0x0C);
    const uint32_t directoryCount = U32(d + 0x1C);
    const uint32_t fileCount = U32(d + 0x28);
    const uint32_t fileTableOffset2 = U32(d + 0x2C);
    const size_t fileTable = d + fileTableOffset;

    std::vector<std::string> directories(directoryCount);
    for (uint32_t i = 0; i < directoryCount; ++i)
        directories[i] = String(fileTableOffset + U32(fileTable + 4 * i));

    m_files.resize(fileCount);
    for (uint32_t i = 0; i < fileCount; ++i)
    {
        const size_t p = fileTable + fileTableOffset2 + i * kFileDescriptorSize;
        if (p + kFileDescriptorSize > m_header.size())
            return Fail("truncated file table");
        File& f = m_files[i];
        f.flags = U16(p);
        f.expandedSize = U64(p + 0x02);
        f.compressedSize = U64(p + 0x0A);
        f.dataOffset = U64(p + 0x12);
        std::memcpy(f.md5, m_header.data() + p + 0x1A, 16);
        const uint32_t nameOffset = U32(p + 0x3A);
        const uint16_t directoryIndex = U16(p + 0x3E);
        f.volume = U16(p + 0x55);
        f.name = String(fileTableOffset + nameOffset);
        if (directoryIndex < directories.size())
            f.directory = directories[directoryIndex];
    }

    // File groups and components are linked lists of (name, descriptor, next) offsets.
    for (int i = 0; i < kMaxFileGroups; ++i)
    {
        for (uint32_t entry = U32(d + 0x3E + 4 * i); entry; entry = U32(d + entry + 8))
        {
            const size_t g = d + U32(d + entry + 4);
            FileGroup group;
            group.name = String(U32(g));
            group.firstFile = U32(g + 4 + 0x12);
            group.lastFile = U32(g + 4 + 0x12 + 4);
            m_fileGroups.push_back(std::move(group));
        }
    }
    for (int i = 0; i < kMaxComponents; ++i)
    {
        for (uint32_t entry = U32(d + 0x15A + 4 * i); entry; entry = U32(d + entry + 8))
        {
            const size_t c = d + U32(d + entry + 4);
            Component component;
            component.name = String(U32(c));
            const size_t p = c + 4 + 0x6B;
            const uint16_t groupCount = U16(p);
            const uint32_t groupTable = U32(p + 2);
            for (uint16_t k = 0; k < groupCount; ++k)
                component.fileGroups.push_back(String(U32(d + groupTable + 4 * k)));
            m_components.push_back(std::move(component));
        }
    }
    return true;
}

Is6Cabinet::Volume* Is6Cabinet::LoadVolume(uint16_t number)
{
    if (m_volumes.size() <= number)
        m_volumes.resize(number + 1);
    Volume& v = m_volumes[number];
    if (v.loaded)
        return &v;
    const auto path = m_folder / (m_prefix + std::to_string(number) + ".CAB");
    if (!ReadWholeFile(path, v.bytes) || Read32(v.bytes, 0) != kSignature)
    {
        Fail("cannot read " + path.string());
        return nullptr;
    }
    const size_t p = kCommonHeaderSize;
    auto u64 = [&](size_t at) { return Read32(v.bytes, at) | (static_cast<uint64_t>(Read32(v.bytes, at + 4)) << 32); };
    v.dataOffset = Read32(v.bytes, p);
    v.firstFile = Read32(v.bytes, p + 0x08);
    v.lastFile = Read32(v.bytes, p + 0x0C);
    v.firstFileOffset = u64(p + 0x10);
    v.firstFileExpanded = u64(p + 0x18);
    v.firstFileCompressed = u64(p + 0x20);
    v.lastFileOffset = u64(p + 0x28);
    v.lastFileExpanded = u64(p + 0x30);
    v.lastFileCompressed = u64(p + 0x38);
    v.loaded = true;
    return &v;
}

// The file's stored bytes (compressed or not), joined across volumes when it is split.
bool Is6Cabinet::ReadRaw(const File& file, size_t index, std::vector<uint8_t>& raw)
{
    const bool compressed = (file.flags & kCompressed) != 0;
    const uint64_t total = compressed ? file.compressedSize : file.expandedSize;
    raw.clear();
    raw.reserve(static_cast<size_t>(total));
    uint16_t number = file.volume;
    Volume* v = LoadVolume(number);
    if (!v)
        return false;

    // Where the data starts in the first volume, and how much of it is there.
    uint64_t offset = file.dataOffset;
    uint64_t available = total;
    if (file.flags & kSplit)
    {
        // A split file is the last one of a volume (or, continuing, the first one of the next).
        if (index == v->lastFile && v->lastFileOffset != 0x7FFFFFFF)
        {
            offset = v->lastFileOffset;
            available = compressed ? v->lastFileCompressed : v->lastFileExpanded;
        }
        else if (index == v->firstFile)
        {
            offset = v->firstFileOffset;
            available = compressed ? v->firstFileCompressed : v->firstFileExpanded;
        }
    }
    while (raw.size() < total)
    {
        const uint64_t take = std::min<uint64_t>(available, total - raw.size());
        if (offset + take > v->bytes.size())
            return Fail("archive volume " + std::to_string(number) + " is truncated");
        raw.insert(raw.end(), v->bytes.begin() + static_cast<ptrdiff_t>(offset), v->bytes.begin() + static_cast<ptrdiff_t>(offset + take));
        if (raw.size() >= total)
            break;
        // Split file: continues at the start of the next volume.
        v = LoadVolume(++number);
        if (!v || v->firstFile != index)
            return Fail("missing continuation of " + file.name + " in volume " + std::to_string(number));
        offset = v->firstFileOffset;
        available = compressed ? v->firstFileCompressed : v->firstFileExpanded;
    }
    return true;
}

bool Is6Cabinet::FileMd5(const std::filesystem::path& path, uint8_t md5[16])
{
    std::vector<uint8_t> bytes;
    if (!ReadWholeFile(path, bytes))
        return false;
    Md5(bytes.data(), bytes.size(), md5);
    return true;
}

std::vector<std::filesystem::path> Is6Cabinet::CandidatesOnMedia(size_t index, const std::filesystem::path& mediaRoot)
{
    std::vector<std::filesystem::path> candidates;
    if (index >= m_files.size() || !m_files[index].IsValid())
        return candidates;
    const File& file = m_files[index];
    if (m_mediaFiles.empty() || m_mediaRoot != mediaRoot)
    {
        m_mediaRoot = mediaRoot;
        m_mediaFiles.clear();
        std::error_code error;
        for (auto it = std::filesystem::recursive_directory_iterator(mediaRoot, error);
             it != std::filesystem::recursive_directory_iterator(); it.increment(error))
        {
            if (error)
                break;
            if (it->is_regular_file(error))
                m_mediaFiles.push_back(it->path());
        }
    }
    const std::filesystem::path expected = mediaRoot / std::filesystem::path(file.directory) / std::filesystem::path(file.name);
    for (const auto& candidate : m_mediaFiles)
    {
        std::error_code error;
        if (_stricmp(candidate.filename().string().c_str(), file.name.c_str()) != 0 ||
            std::filesystem::file_size(candidate, error) != file.expandedSize)
            continue;
        if (_wcsicmp(candidate.c_str(), expected.c_str()) == 0)
            candidates.insert(candidates.begin(), candidate);
        else
            candidates.push_back(candidate);
    }
    return candidates;
}

bool Is6Cabinet::LocateOnMedia(size_t index, const std::filesystem::path& mediaRoot, std::filesystem::path& found)
{
    if (index >= m_files.size() || !m_files[index].IsValid())
        return Fail("invalid file index");
    const File& file = m_files[index];
    for (const auto& candidate : CandidatesOnMedia(index, mediaRoot))
    {
        uint8_t md5[16];
        if (FileMd5(candidate, md5) && std::memcmp(md5, file.md5, 16) == 0)
        {
            found = candidate;
            return true;
        }
    }
    return Fail("not found on the CD: " + (file.directory.empty() ? "" : file.directory + "\\") + file.name);
}

bool Is6Cabinet::Extract(size_t index, std::vector<uint8_t>& data)
{
    if (index >= m_files.size() || !m_files[index].IsValid())
        return Fail("invalid file index");
    const File& file = m_files[index];
    std::vector<uint8_t> raw;
    if (!ReadRaw(file, index, raw))
        return false;
    if (file.flags & kObfuscated)
    {
        uint32_t seed = 0;
        Deobfuscate(raw.data(), raw.size(), seed);
    }

    data.clear();
    data.reserve(static_cast<size_t>(file.expandedSize));
    if (file.flags & kCompressed)
    {
        // A sequence of chunks, each a 16-bit length followed by that many bytes of raw DEFLATE data.
        size_t p = 0;
        while (p + 2 <= raw.size() && data.size() < file.expandedSize)
        {
            const size_t length = raw[p] | (raw[p + 1] << 8);
            p += 2;
            if (p + length > raw.size() || !InflateRaw(raw.data() + p, length, data))
                return Fail("corrupt data in " + file.name);
            p += length;
        }
    }
    else
    {
        data = std::move(raw);
    }
    if (data.size() != file.expandedSize)
        return Fail("wrong size after unpacking " + file.name);

    uint8_t digest[16];
    Md5(data.data(), data.size(), digest);
    if (std::memcmp(digest, file.md5, 16) != 0)
        return Fail("checksum mismatch for " + file.name);
    return true;
}
