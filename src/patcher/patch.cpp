#include "patch.h"

#include "hashes.h"
#include "setup_log.h"
#include "text.h"

#include <windows.h>
#include <tlhelp32.h>

#include <cstring>
#include <fstream>
#include <vector>

namespace
{
struct KnownFile
{
    const wchar_t* name;
    unsigned long long size;
    const char* sha256;
};

// The v3.6 files the patch knows (docs/DESIGN.md, D1). The patch only changes gfxwin.dll, but checks them all: the
// new graphics component and its memory layer were made for exactly these.
const KnownFile kGameFiles[] = {
    {L"manager.exe", 7991296, "bcbb057e80e370dfd1647c7124cac80cd8ac4bdcdc51abb36caa43a828dd63bc"},
    {L"gfxwin.dll", 1097797, "d11c89b34f3d5aef1db32ef38b2259d6b0633ec2aa49fed66e582a00fa12a4c9"},
    {L"dcd3d.dll", 65536, "0844ccb1df64b02c2efd702adebf044f1661eab4fa3b113824b28cfdb405a441"},
    {L"dcsfx.dll", 69632, "583aa62bda79ceda553e748de59cf0f79c6d29583cb37a413a2d497943126bc6"},
    {L"DCSF.dll", 86016, "ec581d48fda3221d2544d154434862613d7c860b64c0ed3eb3209fde1d9a1fff"},
};

// The change to gfxwin.dll: its default graphics backend, an index into its list of backend names
// (dcsf, dcsfx, dcd3d, dcogl), from 1 (dcsfx.dll, which fails on modern Windows) to 3 (dcogl.dll, the new
// component). See docs/PATCHER.md.
constexpr size_t kBackendIndexOffset = 0xFE960;
constexpr uint8_t kOriginalBackend = 1;
constexpr uint8_t kNewBackend = 3;
constexpr const char* kPatchedGfxwinSha256 = "bfa9fef56641836c641aac9764b747c9d5fc10568d165d2e882a67ee96ec1cf5";

const wchar_t* const kPatchFiles[] = {L"dcogl.dll", L"dcogl.ini", L"dcogl.log", L"dcogl.old.log", nullptr};

bool ReadBytes(const std::filesystem::path& path, std::vector<uint8_t>& bytes)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

bool WriteBytes(const std::filesystem::path& path, const void* data, size_t size)
{
    // Written next to the target and then moved over it, so a failure never leaves a half-written file.
    const auto temporary = std::filesystem::path(path).concat(L".new");
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        if (!out)
            return false;
    }
    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);  // some of the game's files come read-only from the CD
    return MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

std::string HexSha256(const void* data, size_t size)
{
    uint8_t digest[32];
    Sha256(data, size, digest);
    char hex[65];
    for (int i = 0; i < 32; ++i)
        snprintf(hex + 2 * i, 3, "%02x", digest[i]);
    return hex;
}

std::string FileSha256(const std::filesystem::path& path)
{
    std::vector<uint8_t> bytes;
    return ReadBytes(path, bytes) ? HexSha256(bytes.data(), bytes.size()) : std::string();
}

// The new graphics component, carried inside this program.
bool EmbeddedComponent(const void*& data, size_t& size)
{
    HRSRC resource = FindResourceW(nullptr, L"DCOGL_DLL", RT_RCDATA);
    HGLOBAL loaded = resource ? LoadResource(nullptr, resource) : nullptr;
    data = loaded ? LockResource(loaded) : nullptr;
    size = resource ? SizeofResource(nullptr, resource) : 0;
    return data && size;
}

std::string EmbeddedComponentSha256()
{
    const void* data;
    size_t size;
    return EmbeddedComponent(data, size) ? HexSha256(data, size) : std::string();
}

std::filesystem::path BackupPath(const std::filesystem::path& gameFolder)
{
    return gameFolder / kPatchFolder / L"gfxwin.dll.original";
}

void OpenLog(const std::filesystem::path& gameFolder)
{
    std::error_code error;
    std::filesystem::create_directories(gameFolder / kPatchFolder, error);
    OpenSetupLog(gameFolder / kPatchFolder / L"pcf2001-patch.log");
    SetupLog(Format(L"pcf2001-patch %hs", PATCH_VERSION));
}
}  // namespace

const wchar_t* const* PatchFileNames()
{
    return kPatchFiles;
}

PatchStatus CheckGame(const std::filesystem::path& gameFolder)
{
    PatchStatus status;
    std::error_code error;
    if (gameFolder.empty() || !std::filesystem::exists(gameFolder / L"manager.exe", error))
    {
        status.details = L"No se encuentra PC FÚTBOL 2001 en esta carpeta (falta manager.exe).";
        return status;
    }
    bool gfxwinPatched = false;
    for (const auto& file : kGameFiles)
    {
        const std::string hash = FileSha256(gameFolder / file.name);
        if (_wcsicmp(file.name, L"gfxwin.dll") == 0 && hash == kPatchedGfxwinSha256)
        {
            gfxwinPatched = true;
            continue;
        }
        if (hash != file.sha256)
        {
            status.state = PatchState::UnknownFiles;
            status.details = hash.empty() ? Format(L"Falta %s.", file.name)
                                          : Format(L"%s no es el de la versión 3.6 original (es otra versión, o está modificado).", file.name);
            status.details += L" El parche solo se aplica a PC FÚTBOL 2001 v3.6, instalado con pcf2001-setup.exe o con las "
                              L"actualizaciones oficiales de Dinamic.";
            return status;
        }
    }
    const std::string component = FileSha256(gameFolder / L"dcogl.dll");
    if (!gfxwinPatched)
    {
        status.state = PatchState::Unpatched;
        status.details = L"PC FÚTBOL 2001 v3.6, sin el parche.";
    }
    else if (component == EmbeddedComponentSha256())
    {
        status.state = PatchState::Patched;
        status.details = Format(L"PC FÚTBOL 2001 v3.6, con el parche %hs aplicado.", PATCH_VERSION);
    }
    else
    {
        status.state = PatchState::PatchedOther;
        status.details = component.empty() ? L"PC FÚTBOL 2001 v3.6, con el parche a medias (falta dcogl.dll). Aplícalo de nuevo."
                                           : L"PC FÚTBOL 2001 v3.6, con otra versión del parche. Aplícalo para actualizarlo.";
    }
    return status;
}

bool GameIsRunning(const std::filesystem::path& gameFolder)
{
    bool running = false;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W entry{sizeof entry};
    for (BOOL more = Process32FirstW(snapshot, &entry); more && !running; more = Process32NextW(snapshot, &entry))
    {
        if (_wcsicmp(entry.szExeFile, L"manager.exe") != 0 && _wcsicmp(entry.szExeFile, L"pcf2001.exe") != 0)
            continue;
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
        wchar_t path[MAX_PATH];
        DWORD length = MAX_PATH;
        if (process && QueryFullProcessImageNameW(process, 0, path, &length))
            running = _wcsicmp(std::filesystem::path(path).parent_path().c_str(), gameFolder.c_str()) == 0;
        else
            running = true;  // can't tell where it runs from: better safe
        if (process)
            CloseHandle(process);
    }
    CloseHandle(snapshot);
    return running;
}

bool ApplyPatch(const std::filesystem::path& gameFolder, std::wstring& error)
{
    const PatchStatus status = CheckGame(gameFolder);
    if (status.state == PatchState::NotAGame || status.state == PatchState::UnknownFiles)
    {
        error = status.details;
        return false;
    }
    if (GameIsRunning(gameFolder))
    {
        error = L"Cierra PC FÚTBOL 2001 antes de aplicar el parche.";
        return false;
    }
    OpenLog(gameFolder);
    SetupLog(L"applying the patch; state before: " + status.details);

    // 1. The new graphics component.
    const void* component;
    size_t componentSize;
    if (!EmbeddedComponent(component, componentSize) || !WriteBytes(gameFolder / L"dcogl.dll", component, componentSize))
    {
        error = L"No se puede escribir dcogl.dll en la carpeta del juego. ¿Tienes permiso para escribir en ella?";
        SetupLog(L"ERROR: cannot write dcogl.dll");
        return false;
    }
    SetupLog(L"dcogl.dll installed");

    // 2. gfxwin.dll: back up the original, then change the default backend.
    if (status.state == PatchState::Unpatched)
    {
        std::vector<uint8_t> gfxwin;
        if (!ReadBytes(gameFolder / L"gfxwin.dll", gfxwin) || gfxwin.size() <= kBackendIndexOffset ||
            gfxwin[kBackendIndexOffset] != kOriginalBackend)
        {
            error = L"gfxwin.dll no tiene el contenido esperado.";
            return false;
        }
        if (!WriteBytes(BackupPath(gameFolder), gfxwin.data(), gfxwin.size()))
        {
            error = L"No se puede guardar la copia de seguridad de gfxwin.dll.";
            SetupLog(L"ERROR: cannot back up gfxwin.dll");
            return false;
        }
        gfxwin[kBackendIndexOffset] = kNewBackend;
        if (HexSha256(gfxwin.data(), gfxwin.size()) != kPatchedGfxwinSha256 || !WriteBytes(gameFolder / L"gfxwin.dll", gfxwin.data(), gfxwin.size()))
        {
            error = L"No se puede modificar gfxwin.dll.";
            SetupLog(L"ERROR: cannot write gfxwin.dll");
            return false;
        }
        SetupLog(L"gfxwin.dll: default graphics backend changed to dcogl.dll (original saved)");
    }

    const PatchStatus after = CheckGame(gameFolder);
    SetupLog(L"state after: " + after.details);
    if (after.state != PatchState::Patched)
    {
        error = L"El parche no ha quedado bien aplicado: " + after.details;
        return false;
    }
    return true;
}

bool RemovePatch(const std::filesystem::path& gameFolder, std::wstring& error)
{
    const PatchStatus status = CheckGame(gameFolder);
    if (status.state != PatchState::Patched && status.state != PatchState::PatchedOther)
    {
        error = status.state == PatchState::Unpatched ? L"El parche no está aplicado." : status.details;
        return false;
    }
    if (GameIsRunning(gameFolder))
    {
        error = L"Cierra PC FÚTBOL 2001 antes de quitar el parche.";
        return false;
    }
    OpenLog(gameFolder);
    SetupLog(L"removing the patch");

    // The original gfxwin.dll, from the backup if it's intact; otherwise by undoing the one-byte change.
    std::vector<uint8_t> gfxwin;
    const auto backup = BackupPath(gameFolder);
    if (!ReadBytes(backup, gfxwin) || HexSha256(gfxwin.data(), gfxwin.size()) != kGameFiles[1].sha256)
    {
        SetupLog(L"backup of gfxwin.dll missing or damaged; undoing the change in place");
        if (!ReadBytes(gameFolder / L"gfxwin.dll", gfxwin) || gfxwin.size() <= kBackendIndexOffset)
        {
            error = L"No se puede leer gfxwin.dll.";
            return false;
        }
        gfxwin[kBackendIndexOffset] = kOriginalBackend;
    }
    if (HexSha256(gfxwin.data(), gfxwin.size()) != kGameFiles[1].sha256 || !WriteBytes(gameFolder / L"gfxwin.dll", gfxwin.data(), gfxwin.size()))
    {
        error = L"No se puede restaurar gfxwin.dll.";
        SetupLog(L"ERROR: cannot restore gfxwin.dll");
        return false;
    }
    SetupLog(L"gfxwin.dll restored");

    OpenSetupLog({});  // the log is in the folder about to be removed
    std::error_code fsError;
    for (const wchar_t* const* name = kPatchFiles; *name; ++name)
        std::filesystem::remove(gameFolder / *name, fsError);
    std::filesystem::remove_all(gameFolder / kPatchFolder, fsError);

    if (CheckGame(gameFolder).state != PatchState::Unpatched)
    {
        error = L"El parche no se ha podido quitar del todo.";
        return false;
    }
    return true;
}
