#include "game_files.h"

#include "hashes.h"
#include "setup_log.h"
#include "text.h"

#include <windows.h>

#include <cstring>
#include <fstream>

namespace
{
// File groups of the archive that Dinamic's setup installed into the game folder ("Typical" setup, everything on
// disk). Left out: the InstallShield engine and setup files (<Engine>, <Support>, <Disk1>), "Shared DLLs" (old
// Microsoft runtimes that went into System32, which today's Windows already has) and "Externos" (DirectX and
// Internet Explorer 5 installers).
const char* const kInstalledGroups[] = {"Exe & Dll", "Root", "MMX", "Cond_Base Datos", "Cond_Graficos", "Cond_Simulador",
                                        "Cond_Sonido", "Setup"};

bool Installed(const Is6Cabinet::FileGroup& group, const Is6Cabinet::File& file)
{
    bool listed = false;
    for (const char* name : kInstalledGroups)
        listed = listed || group.name == name;
    if (!listed)
        return false;
    // Utils.dll exists twice: a plain build, and one using MMX instructions (group "MMX"), which setup chose on any
    // processor with MMX. Every processor that runs Windows 10 has it.
    if (group.name == "Exe & Dll" && _stricmp(file.name.c_str(), "utils.dll") == 0)
        return false;
    // From the CD's own setup files, only the icon and the intro video were installed; the rest (autorun, DISK.ID)
    // stays on the CD, where the game checks for DISK.ID.
    if (group.name == "Setup")
        return _stricmp(file.name.c_str(), "PCF2001.ico") == 0 || _stricmp(file.name.c_str(), "IntroPCF.mpg") == 0;
    return true;
}

bool ReadAll(const std::filesystem::path& path, std::string& text)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

bool WriteFileData(const std::filesystem::path& path, const void* data, size_t size)
{
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    return static_cast<bool>(out);
}
}  // namespace

bool IsGameCd(const std::filesystem::path& root)
{
    std::string id;
    std::error_code error;
    return ReadAll(root / L"DISK.ID", id) && id.rfind("PCF9", 0) == 0 && std::filesystem::exists(root / L"DATA1.HDR", error);
}

std::vector<std::filesystem::path> FindGameCds()
{
    std::vector<std::filesystem::path> found;
    const DWORD drives = GetLogicalDrives();
    for (int i = 0; i < 26; ++i)
    {
        if (!(drives & (1u << i)))
            continue;
        const wchar_t root[] = {static_cast<wchar_t>(L'A' + i), L':', L'\\', 0};
        if (GetDriveTypeW(root) != DRIVE_CDROM)
            continue;
        // Avoid Windows' "insert a disc" dialog for empty drives.
        const UINT previous = SetErrorMode(SEM_FAILCRITICALERRORS);
        const bool isGame = IsGameCd(root);
        SetErrorMode(previous);
        if (isGame)
            found.emplace_back(root);
    }
    return found;
}

bool GameFiles::Prepare(const std::filesystem::path& cdRoot, std::wstring& error)
{
    m_cdRoot = cdRoot;
    m_items.clear();
    if (!m_cabinet.Open(cdRoot / L"DATA1.HDR"))
    {
        error = L"No se puede leer el instalador del CD: " + FromUtf8(m_cabinet.Error());
        return false;
    }
    const auto& files = m_cabinet.Files();
    for (const auto& group : m_cabinet.FileGroups())
    {
        for (uint32_t i = group.firstFile; i <= group.lastFile && i < files.size(); ++i)
        {
            const auto& file = files[i];
            if (!file.IsValid() || !Installed(group, file))
                continue;
            std::filesystem::path destination = FromCp1252(file.directory);
            destination /= FromCp1252(file.name);
            m_items.push_back({i, destination, file.expandedSize});
        }
    }
    if (m_items.empty())
    {
        error = L"El CD no contiene los archivos esperados de PC FÚTBOL 2001.";
        return false;
    }
    return true;
}

uint64_t GameFiles::TotalBytes() const
{
    uint64_t total = 0;
    for (const auto& item : m_items)
        total += item.size;
    return total;
}

std::vector<std::filesystem::path> GameFiles::EmptyFolders()
{
    // Listed at the end of the archive's header, created by setup for every installation.
    return {L"Goles", L"Notas", L"Save\\Manager"};
}

bool GameFiles::CopyFromCd(const Item& item, const std::filesystem::path& destination, uint64_t& done,
                           const std::function<bool(uint64_t)>& progress, std::wstring& error)
{
    const auto& file = m_cabinet.Files()[item.index];
    const auto candidates = m_cabinet.CandidatesOnMedia(item.index, m_cdRoot);
    std::error_code fsError;
    std::filesystem::create_directories(destination.parent_path(), fsError);
    const auto partial = std::filesystem::path(destination).concat(L".part");
    for (const auto& source : candidates)
    {
        std::ifstream in(source, std::ios::binary);
        std::ofstream out(partial, std::ios::binary | std::ios::trunc);
        if (!in || !out)
            continue;
        Md5Hasher hasher;
        std::vector<char> buffer(1 << 20);
        uint64_t copied = 0;
        while (in)
        {
            in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto count = static_cast<size_t>(in.gcount());
            if (count == 0)
                break;
            hasher.Update(buffer.data(), count);
            out.write(buffer.data(), static_cast<std::streamsize>(count));
            copied += count;
            if (!progress(done + copied))
            {
                out.close();
                std::filesystem::remove(partial, fsError);
                error = L"Instalación cancelada.";
                return false;
            }
        }
        out.close();
        uint8_t md5[16];
        hasher.Finish(md5);
        if (!out || copied != file.expandedSize || std::memcmp(md5, file.md5, 16) != 0)
        {
            SetupLog(L"  copy of " + LoggablePath(source, m_cdRoot) + L" doesn't match the archive's checksum; trying another");
            continue;
        }
        std::filesystem::rename(partial, destination, fsError);
        if (fsError)
        {
            error = L"No se puede escribir " + destination.native();
            return false;
        }
        done += copied;
        return true;
    }
    std::filesystem::remove(partial, fsError);
    error = L"Falta en el CD, o está dañado: " + item.destination.native();
    return false;
}

bool GameFiles::Install(const std::filesystem::path& target, const std::function<bool(uint64_t)>& progress,
                        std::vector<std::filesystem::path>& written, std::wstring& error)
{
    SetupLog(Format(L"CD: %zu files to install, %.1f MB", m_items.size(), TotalBytes() / (1024.0 * 1024.0)));
    uint64_t done = 0;
    std::vector<uint8_t> data;
    for (const auto& item : m_items)
    {
        const auto destination = target / item.destination;
        const auto& file = m_cabinet.Files()[item.index];
        if (file.InArchive())
        {
            if (!m_cabinet.Extract(item.index, data))
            {
                error = L"No se puede extraer " + item.destination.native() + L" del CD: " + FromUtf8(m_cabinet.Error());
                return false;
            }
            if (!WriteFileData(destination, data.data(), data.size()))
            {
                error = L"No se puede escribir " + destination.native();
                return false;
            }
            done += item.size;
            if (!progress(done))
            {
                error = L"Instalación cancelada.";
                return false;
            }
        }
        else if (!CopyFromCd(item, destination, done, progress, error))
        {
            return false;
        }
        written.push_back(destination);
    }
    for (const auto& folder : EmptyFolders())
    {
        std::error_code fsError;
        std::filesystem::create_directories(target / folder, fsError);
    }
    SetupLog(Format(L"CD: %zu files installed", written.size()));
    return true;
}
