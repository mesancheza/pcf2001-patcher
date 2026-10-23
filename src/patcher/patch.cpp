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

// The v3.6 files the patch knows (docs/DESIGN.md, D1). The patch changes some of them (kPatchedFiles), but checks
// them all: the new graphics component and its memory layer were made for exactly these.
const KnownFile kGameFiles[] = {
    {L"manager.exe", 7991296, "bcbb057e80e370dfd1647c7124cac80cd8ac4bdcdc51abb36caa43a828dd63bc"},
    {L"gfxwin.dll", 1097797, "d11c89b34f3d5aef1db32ef38b2259d6b0633ec2aa49fed66e582a00fa12a4c9"},
    {L"dcd3d.dll", 65536, "0844ccb1df64b02c2efd702adebf044f1661eab4fa3b113824b28cfdb405a441"},
    {L"dcsfx.dll", 69632, "583aa62bda79ceda553e748de59cf0f79c6d29583cb37a413a2d497943126bc6"},
    {L"DCSF.dll", 86016, "ec581d48fda3221d2544d154434862613d7c860b64c0ed3eb3209fde1d9a1fff"},
};

// Bytes at a file offset, as they are in v3.6 and as the patch leaves them.
struct ByteChange
{
    size_t offset;
    std::vector<uint8_t> original;
    std::vector<uint8_t> patched;
};

// A game file the patch changes. The original is backed up in the patch folder before the first change.
struct PatchedFile
{
    const KnownFile& file;
    const char* patchedSha256;
    const wchar_t* description;  // for the log
    std::vector<ByteChange> changes;
};

// See docs/PATCHER.md for what each change does.
const PatchedFile kPatchedFiles[] = {
    // The default graphics backend, an index into gfxwin.dll's list of backend names (dcsf, dcsfx, dcd3d, dcogl):
    // from 1 (dcsfx.dll, which fails on modern Windows) to 3 (dcogl.dll, the new component).
    {kGameFiles[1], "bfa9fef56641836c641aac9764b747c9d5fc10568d165d2e882a67ee96ec1cf5",
     L"default graphics backend changed to dcogl.dll",
     {{0xFE960, {0x01}, {0x03}}}},
    // Fixes in the game's code (docs/GAME-INTERNALS.md). Each one jumps to new code in the padding between two
    // functions, and back.
    {kGameFiles[0], "8881a45c1baef4125cb2a5e786e4494f5b2989a35f846d00bbc885c7b8c7bd49",
     L"advertising boards on Auto without a groundskeeper, absurd and empty TV offers, boards screen after "
     L"Rellena, two services works at once, services cost after a cafeteria is finished, fixed",
     // Advertising boards on Auto with no groundskeeper hired: where the daily processing reads the groundskeeper's
     // quality through a null pointer, use quality 0 when there's none, as the assistant handles the boards.
     {{0xABA8C,
       {0x33, 0xC9, 0x8A, 0x48, 0x01, 0x89, 0x4C, 0x24, 0x20},   // xor ecx,ecx; mov cl,[eax+1]; mov [esp+20h],ecx
       {0xE9, 0xD0, 0xFB, 0xFF, 0xFF, 0x89, 0x44, 0x24, 0x20}},  // jmp 0x4AB661; mov [esp+20h],eax
      {0xAB661,
       {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90},
       {0x85, 0xC0,                     // test eax,eax
        0x74, 0x04,                     // je 0x4AB669 (no groundskeeper: quality 0)
        0x0F, 0xB6, 0x40, 0x01,         // movzx eax,byte ptr [eax+1]
        0xE9, 0x23, 0x04, 0x00, 0x00}},  // jmp 0x4ABA91
      // Absurd TV offers in lower divisions: the TV offer constructor never sets the "all rights" amount for teams
      // of level 7 or higher (the lowest divisions), and scales whatever the new memory held. Set it to 0 there, as
      // the other offers show.
      {0x928FD,
       {0x89, 0x6E, 0x18, 0xC6, 0x46, 0x1C, 0x00},   // mov [esi+18h],ebp; mov byte ptr [esi+1Ch],0
       {0xE9, 0x4F, 0xF6, 0xFF, 0xFF, 0x90, 0x90}},  // jmp 0x491F51; nop; nop
      {0x91F51,
       {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90},
       {0x89, 0x6E, 0x18,                 // mov [esi+18h],ebp
        0xC6, 0x46, 0x1C, 0x00,           // mov byte ptr [esi+1Ch],0
        0x89, 0x2E,                       // mov [esi],ebp (ebp is 0: no "all rights" amount)
        0xE9, 0xA5, 0x09, 0x00, 0x00}},  // jmp 0x492904
      // Empty TV offers: when a new offer has no money at all (no "all rights" amount, nothing per match), delete it
      // instead of adding it to the team's offers (0x4A0F10). The check and the delete use two padding runs a short
      // jump apart.
      {0x9CB6E,
       {0x8B, 0xCE, 0xE8, 0x9B, 0x43, 0x00, 0x00},   // mov ecx,esi; call 0x4A0F10
       {0xE8, 0xAF, 0x2E, 0x00, 0x00, 0x90, 0x90}},  // call 0x49FA22; nop; nop
      {0x9FA22,
       {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90},
       {0x0B, 0x51, 0x08,                 // or edx,[ecx+8] (edx: the "all rights" amount; ecx: the offer)
        0x8B, 0xCE,                       // mov ecx,esi
        0x74, 0x38,                       // je 0x49FA61 (no money)
        0xE9, 0xE2, 0x14, 0x00, 0x00}},   // jmp 0x4A0F10
      {0x9FA61,
       {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90},
       {0xFF, 0x74, 0x24, 0x04,           // push dword ptr [esp+4] (the offer)
        0xE8, 0xA0, 0xAA, 0x53, 0x00,     // call 0x9DA50A (operator delete)
        0x83, 0xC4, 0x04,                 // add esp,4
        0xC2, 0x04, 0x00}},               // ret 4, as 0x4A0F10 does
      // Boards screen after Rellena (also run when Auto is switched on): the last board sold in the last section
      // stays on screen as unsold. After the loop over the sections, show the current section again, as the loop
      // does before each one. Three padding runs, linked by short jumps.
      {0xE593B,
       {0x3B, 0xC3, 0xC6, 0x44, 0x24, 0x68, 0x03},   // cmp eax,ebx; mov byte ptr [esp+68h],3
       {0xE9, 0x81, 0x76, 0xFE, 0xFF, 0x90, 0x90}},  // jmp 0x4CCFC1; nop; nop
      {0xCCFC1,
       {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90},
       {0xC6, 0x44, 0x24, 0x68, 0x03,     // mov byte ptr [esp+68h],3
        0x6A, 0x00,                       // push 0 (from the top of the list)
        0xFF, 0xB5, 0xDC, 0x21, 0x00, 0x00,  // push dword ptr [ebp+21DCh] (the current section)
        0xEB, 0x31}},                     // jmp 0x4CD001
      {0xCD001,
       {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90},
       {0x8B, 0xCD,                       // mov ecx,ebp
        0xE8, 0x08, 0x81, 0x01, 0x00,     // call 0x4E5110 (fill the section's list)
        0x6A, 0x01,                       // push 1
        0x8B, 0xCD,                       // mov ecx,ebp
        0xEB, 0x33}},                     // jmp 0x4CD041
      {0xCD041,
       {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90},
       {0xFF, 0x15, 0x60, 0xA0, 0xA6, 0x00,  // call dword ptr [0xA6A060] (redraw)
        0x33, 0xC0,                       // xor eax,eax (always 0 here; sets ZF for the je that follows)
        0xE9, 0xF4, 0x88, 0x01, 0x00}},   // jmp 0x4E5942
      // Two services works at once: the Remodelar screen lets a Cafeterías work be ordered while an Enfermería,
      // Tienda or Lavabos work (the stadium's +0x454) is pending, and the two share one weekly countdown. Enable
      // the Cafeterías options only when neither kind of services work is pending, as the other rows already do.
      {0xDE536,
       {0x8B, 0x88, 0x64, 0x04, 0x00, 0x00},         // mov ecx,[eax+464h]
       {0xE8, 0xE6, 0x0E, 0x00, 0x00, 0x90}},        // call 0x4DF421; nop
      {0xDF421,
       {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90},
       {0x8B, 0x88, 0x64, 0x04, 0x00, 0x00,  // mov ecx,[eax+464h] (pending Cafeterías work, -1 if none)
        0x23, 0x88, 0x54, 0x04, 0x00, 0x00,  // and ecx,[eax+454h] (pending other services work): -1 only if both are
        0xC3}},                           // ret
      // Services cost after a cafeteria is finished: the weekly update marks the work done, sets the current
      // cafeteria work (+0x464) to -1, then recomputes the services' weekly cost from "entry -1", reading two
      // unrelated fields as weeks and cost. Rewritten more compactly in place, the block ends by setting the
      // services' weekly cost and weeks to 0 instead (skipping the increment of the code for a work with 0 weeks
      // left, which would leave weeks at 1: a "works finished" message from the groundskeeper every week).
      {0xCD91E,
       {0x8B, 0x86, 0x64, 0x04, 0x00, 0x00, 0x6A, 0x03, 0x8D, 0x0C, 0xC5, 0x00, 0x00, 0x00, 0x00, 0x2B, 0xC8, 0xC6,
        0x84, 0x8E, 0xB8, 0x01, 0x00, 0x00, 0x01, 0x8A, 0x56, 0x0D, 0xFE, 0xC2, 0x8B, 0xCB, 0x88, 0x56, 0x0D, 0xC7,
        0x86, 0x64, 0x04, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xE8, 0x50, 0x50, 0xFF, 0xFF, 0x6A, 0x02, 0x8B, 0xCB,
        0xE8, 0x77, 0x50, 0xFF, 0xFF},
       {0x8B, 0x86, 0x64, 0x04, 0x00, 0x00,          // mov eax,[esi+464h]
        0x6A, 0x03,                                  // push 3
        0x8D, 0x0C, 0xC5, 0x00, 0x00, 0x00, 0x00,    // lea ecx,[eax*8]
        0x2B, 0xC8,                                  // sub ecx,eax
        0xC6, 0x84, 0x8E, 0xB8, 0x01, 0x00, 0x00, 0x01,  // mov byte ptr [esi+ecx*4+1B8h],1 (finished)
        0xFE, 0x46, 0x0D,                            // inc byte ptr [esi+0Dh] (was mov dl / inc dl / mov back)
        0x8B, 0xCB,                                  // mov ecx,ebx
        0x83, 0x8E, 0x64, 0x04, 0x00, 0x00, 0xFF,    // or dword ptr [esi+464h],-1 (was a 10-byte mov)
        0xE8, 0x58, 0x50, 0xFF, 0xFF,                // call 0x4C29A0 (moved: same target)
        0x6A, 0x02,                                  // push 2
        0x8B, 0xCB,                                  // mov ecx,ebx
        0xE8, 0x7F, 0x50, 0xFF, 0xFF,                // call 0x4C29D0 (moved: same target)
        0x33, 0xC0,                                  // xor eax,eax
        0xEB, 0x42,                                  // jmp 0x4CD997 (weekly cost 0, weeks eax = 0)
        0x90, 0x90, 0x90, 0x90}}}},
};

const PatchedFile* FindPatchedFile(const wchar_t* name)
{
    for (const auto& patched : kPatchedFiles)
    {
        if (_wcsicmp(patched.file.name, name) == 0)
            return &patched;
    }
    return nullptr;
}

// Makes the changes (or undoes them), after checking every location holds the bytes expected before.
bool ChangeBytes(std::vector<uint8_t>& bytes, const PatchedFile& patched, bool apply)
{
    for (const auto& change : patched.changes)
    {
        const auto& from = apply ? change.original : change.patched;
        if (change.offset + from.size() > bytes.size() || memcmp(&bytes[change.offset], from.data(), from.size()) != 0)
            return false;
    }
    for (const auto& change : patched.changes)
    {
        const auto& to = apply ? change.patched : change.original;
        memcpy(&bytes[change.offset], to.data(), to.size());
    }
    return true;
}

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

std::filesystem::path BackupPath(const std::filesystem::path& gameFolder, const wchar_t* name)
{
    return gameFolder / kPatchFolder / (std::wstring(name) + L".original");
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
    size_t patchedFiles = 0;
    for (const auto& file : kGameFiles)
    {
        const std::string hash = FileSha256(gameFolder / file.name);
        const PatchedFile* patched = FindPatchedFile(file.name);
        if (patched && hash == patched->patchedSha256)
        {
            ++patchedFiles;
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
    // Partly changed files mean another version of the patch: earlier versions changed fewer files.
    const std::string component = FileSha256(gameFolder / L"dcogl.dll");
    if (patchedFiles == 0)
    {
        status.state = PatchState::Unpatched;
        status.details = L"PC FÚTBOL 2001 v3.6, sin el parche.";
    }
    else if (patchedFiles == std::size(kPatchedFiles) && component == EmbeddedComponentSha256())
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

    // 2. The changed game files: back up each original, then change it. Files already changed are left as they are
    // (CheckGame accepted every file as either the original or the changed one).
    for (const auto& patched : kPatchedFiles)
    {
        const auto path = gameFolder / patched.file.name;
        std::vector<uint8_t> bytes;
        if (!ReadBytes(path, bytes))
        {
            error = Format(L"No se puede leer %s.", patched.file.name);
            return false;
        }
        if (HexSha256(bytes.data(), bytes.size()) == patched.patchedSha256)
            continue;
        if (!WriteBytes(BackupPath(gameFolder, patched.file.name), bytes.data(), bytes.size()))
        {
            error = Format(L"No se puede guardar la copia de seguridad de %s.", patched.file.name);
            SetupLog(Format(L"ERROR: cannot back up %s", patched.file.name));
            return false;
        }
        if (!ChangeBytes(bytes, patched, true))
        {
            error = Format(L"%s no tiene el contenido esperado.", patched.file.name);
            return false;
        }
        if (HexSha256(bytes.data(), bytes.size()) != patched.patchedSha256 || !WriteBytes(path, bytes.data(), bytes.size()))
        {
            error = Format(L"No se puede modificar %s.", patched.file.name);
            SetupLog(Format(L"ERROR: cannot write %s", patched.file.name));
            return false;
        }
        SetupLog(Format(L"%s: %s (original saved)", patched.file.name, patched.description));
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

    // Each changed file's original, from the backup if it's intact; otherwise by undoing the changes in place. Files
    // still original (an earlier version of the patch didn't change them) are left alone.
    for (const auto& patched : kPatchedFiles)
    {
        const auto path = gameFolder / patched.file.name;
        if (FileSha256(path) == patched.file.sha256)
            continue;
        std::vector<uint8_t> bytes;
        if (!ReadBytes(BackupPath(gameFolder, patched.file.name), bytes) || HexSha256(bytes.data(), bytes.size()) != patched.file.sha256)
        {
            SetupLog(Format(L"backup of %s missing or damaged; undoing the changes in place", patched.file.name));
            if (!ReadBytes(path, bytes) || !ChangeBytes(bytes, patched, false))
            {
                error = Format(L"No se puede restaurar %s.", patched.file.name);
                return false;
            }
        }
        if (HexSha256(bytes.data(), bytes.size()) != patched.file.sha256 || !WriteBytes(path, bytes.data(), bytes.size()))
        {
            error = Format(L"No se puede restaurar %s.", patched.file.name);
            SetupLog(Format(L"ERROR: cannot restore %s", patched.file.name));
            return false;
        }
        SetupLog(Format(L"%s restored", patched.file.name));
    }

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
