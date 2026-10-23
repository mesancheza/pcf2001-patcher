#include "mods.h"

#include "setup_log.h"
#include "text.h"

#include <windows.h>

#include <cstring>
#include <map>

namespace
{
// See docs/MODS.md for what each mod does. The offsets are file offsets; in manager.exe the VA is the offset plus
// 0x400000. No two mods that change the same bytes can be installed together.
const std::vector<Mod> kMods = {
    // Youth players from any country: the youth search (0x48DCF0) takes the players of the hidden team Juveniles
    // Españoles and checks each one (0x48D9E0). For a search made for a team, the check also rejects the players
    // whose country isn't the team's, and those the team's competition rules out (its rule on foreign players). Skip
    // that block, as for a search made for no team; the other conditions stay: not already in the team, 18 or
    // younger, and the positions searched.
    {L"juveniles-de-cualquier-pais", L"Jóvenes promesas de cualquier país",
     L"La búsqueda de jóvenes promesas encuentra juveniles de cualquier país, no solo del país del club, y sin las "
     L"reglas de extranjeros de su competición.",
     L"manager.exe",
     {{0x8DA2B,
       {0x0F, 0x84, 0xA6, 0x00, 0x00, 0x00},     // je 0x48DAD7 (search made for no team)
       {0xE9, 0xA7, 0x00, 0x00, 0x00, 0x90}}}},  // jmp 0x48DAD7; nop
    // No limit on foreign players in the squad: every limit check (signings, offers, youth promotion, the other
    // clubs' transfers) compares the club's foreign players, counted by 0x49A7E0, with its competition's maximum
    // (virtual method +0x8C). Count none; and in the three competitions whose maximum is 0, point that method at
    // the one the competitions without a limit use (0x53FD80, 65535). Also lets foreign players be offered to clubs
    // that refused them.
    {L"extranjeros-sin-limite-en-plantilla", L"Sin límite de extranjeros en la plantilla",
     L"Se pueden fichar todos los extranjeros que se quiera, también en las categorías que no los admiten. Vale para "
     L"todos los clubes.",
     L"manager.exe",
     {{0x9A7E0, {0x51, 0x64, 0x8B}, {0x33, 0xC0, 0xC3}},       // push ecx; mov edx,fs:[...] -> xor eax,eax; ret
      {0x67A728, {0x30, 0xCE, 0x40, 0x00}, {0x80, 0xFD, 0x53, 0x00}},  // vtable 0xA7A69C +0x8C: 0x40CE30 (0) -> 0x53FD80
      {0x67CC6C, {0x30, 0xCE, 0x40, 0x00}, {0x80, 0xFD, 0x53, 0x00}},  // vtable 0xA7CBE0 +0x8C
      {0x67CF84, {0x30, 0xCE, 0x40, 0x00}, {0x80, 0xFD, 0x53, 0x00}},  // vtable 0xA7CEF8 +0x8C
      {0x1064D8, {0x74, 0x20}, {0xEB, 0x20}}}},                // je -> jmp 0x5064FA: no "No puedes ofrecer..."
    // No limit on foreign players in line-ups: the six calls of the competition's line-up maximum (virtual method
    // +0x94) return 65535 instead: confirming the line-up (0x969068), swapping players (0x41F4F8) and the automatic
    // line-ups (the other four). Each 6-byte call becomes a 6-byte mov.
    {L"extranjeros-sin-limite-en-alineacion", L"Sin límite de extranjeros en las alineaciones",
     L"Se pueden alinear todos los extranjeros que se quiera. Vale para todos los clubes, también en sus alineaciones "
     L"automáticas.",
     L"manager.exe",
     {{0x1F4F8, {0xFF, 0x92, 0x94, 0x00, 0x00, 0x00}, {0xB8, 0xFF, 0xFF, 0x00, 0x00, 0x90}},  // call [edx+94h] -> mov eax,0FFFFh
      {0x93470, {0xFF, 0x92, 0x94, 0x00, 0x00, 0x00}, {0xB8, 0xFF, 0xFF, 0x00, 0x00, 0x90}},
      {0x93CBF, {0xFF, 0x90, 0x94, 0x00, 0x00, 0x00}, {0xB8, 0xFF, 0xFF, 0x00, 0x00, 0x90}},  // call [eax+94h]
      {0x944FC, {0xFF, 0x92, 0x94, 0x00, 0x00, 0x00}, {0xB8, 0xFF, 0xFF, 0x00, 0x00, 0x90}},
      {0x95D49, {0xFF, 0x90, 0x94, 0x00, 0x00, 0x00}, {0xB8, 0xFF, 0xFF, 0x00, 0x00, 0x90}},
      {0x569068, {0xFF, 0x92, 0x94, 0x00, 0x00, 0x00}, {0xB8, 0xFF, 0xFF, 0x00, 0x00, 0x90}}}},
};

bool Matches(const std::vector<uint8_t>& bytes, size_t offset, const std::vector<uint8_t>& expected)
{
    return offset + expected.size() <= bytes.size() && memcmp(&bytes[offset], expected.data(), expected.size()) == 0;
}

bool HasBytes(const Mod& mod, const std::vector<uint8_t>& bytes, bool installed)
{
    for (const auto& change : mod.changes)
    {
        if (!Matches(bytes, change.offset, installed ? change.patched : change.original))
            return false;
    }
    return true;
}

void WriteMod(std::vector<uint8_t>& bytes, const Mod& mod, bool install)
{
    for (const auto& change : mod.changes)
    {
        const auto& to = install ? change.patched : change.original;
        memcpy(&bytes[change.offset], to.data(), to.size());
    }
}

bool Overlap(const Mod& a, const Mod& b)
{
    if (_wcsicmp(a.file, b.file) != 0)
        return false;
    for (const auto& x : a.changes)
    {
        for (const auto& y : b.changes)
        {
            if (x.offset < y.offset + y.original.size() && y.offset < x.offset + x.original.size())
                return true;
        }
    }
    return false;
}

struct ModFile
{
    std::vector<uint8_t> bytes;  // as found
    std::vector<uint8_t> base;   // without mods: as the patch leaves it, if the check passed
};

// Reads every file the mods change, and checks that without its mods each is as this version of the patch leaves
// it. Sets an error text for the player otherwise.
bool ReadModFiles(const std::filesystem::path& gameFolder, std::map<std::wstring, ModFile>& files, std::wstring& error)
{
    for (const auto& mod : kMods)
    {
        if (files.count(mod.file))
            continue;
        ModFile& file = files[mod.file];
        if (!ReadBytes(gameFolder / mod.file, file.bytes))
        {
            error = Format(L"No se encuentra PC FÚTBOL 2001 en esta carpeta (falta %s).", mod.file);
            return false;
        }
        file.base = file.bytes;
        if (!RemoveInstalledMods(mod.file, file.base) || HexSha256(file.base.data(), file.base.size()) != PatchedSha256(mod.file))
        {
            error = Format(L"%s no es el que deja el parche %hs. Los mods se instalan sobre el parche: aplícalo antes con "
                           L"pcf2001-patch.exe, de la misma versión que este programa.",
                           mod.file, PATCH_VERSION);
            return false;
        }
    }
    return true;
}
}  // namespace

const std::vector<Mod>& Mods()
{
    return kMods;
}

bool RemoveInstalledMods(const wchar_t* fileName, std::vector<uint8_t>& bytes)
{
    for (const auto& mod : kMods)
    {
        if (_wcsicmp(mod.file, fileName) == 0 && HasBytes(mod, bytes, true))
            WriteMod(bytes, mod, false);
    }
    // Only now: a mod's original bytes may have been another's, installed, until that one was undone.
    for (const auto& mod : kMods)
    {
        if (_wcsicmp(mod.file, fileName) == 0 && !HasBytes(mod, bytes, false))
            return false;
    }
    return true;
}

ModsStatus CheckMods(const std::filesystem::path& gameFolder)
{
    ModsStatus status;
    std::map<std::wstring, ModFile> files;
    if (gameFolder.empty())
    {
        status.details = L"Elige la carpeta donde está instalado el juego.";
        return status;
    }
    if (!ReadModFiles(gameFolder, files, status.details))
        return status;
    // ReadModFiles checked every mod's original bytes in the file without mods, so a mod is either installed or not.
    size_t installed = 0;
    for (const auto& mod : kMods)
    {
        status.installed.push_back(HasBytes(mod, files[mod.file].bytes, true));
        if (status.installed.back())
            ++installed;
    }
    status.ready = true;
    status.details = Format(L"PC FÚTBOL 2001 v3.6 con el parche %hs. Mods instalados: %zu de %zu.", PATCH_VERSION, installed,
                            kMods.size());
    return status;
}

bool SetMods(const std::filesystem::path& gameFolder, const std::vector<bool>& wanted, std::wstring& error)
{
    if (wanted.size() != kMods.size())
    {
        error = L"Lista de mods incorrecta.";
        return false;
    }
    for (size_t i = 0; i < kMods.size(); ++i)
    {
        for (size_t j = i + 1; j < kMods.size(); ++j)
        {
            if (wanted[i] && wanted[j] && Overlap(kMods[i], kMods[j]))
            {
                error = Format(L"\"%s\" y \"%s\" cambian lo mismo y no se pueden instalar juntos.", kMods[i].name, kMods[j].name);
                return false;
            }
        }
    }
    std::map<std::wstring, ModFile> files;
    if (!ReadModFiles(gameFolder, files, error))
        return false;
    if (GameIsRunning(gameFolder))
    {
        error = L"Cierra PC FÚTBOL 2001 antes de cambiar los mods.";
        return false;
    }

    std::error_code fsError;
    std::filesystem::create_directories(gameFolder / kPatchFolder, fsError);
    OpenSetupLog(gameFolder / kPatchFolder / L"pcf2001-mods.log");
    SetupLog(Format(L"pcf2001-mods %hs", PATCH_VERSION));

    // Each file is rebuilt from its contents without mods, with the wanted ones, and written only if it changes.
    for (auto& [name, file] : files)
    {
        std::vector<uint8_t> result = file.base;
        for (size_t i = 0; i < kMods.size(); ++i)
        {
            if (wanted[i] && name == kMods[i].file)
                WriteMod(result, kMods[i], true);
        }
        if (result == file.bytes)
            continue;
        if (!WriteBytes(gameFolder / name, result.data(), result.size()))
        {
            error = Format(L"No se puede modificar %s.", name.c_str());
            SetupLog(Format(L"ERROR: cannot write %s", name.c_str()));
            return false;
        }
        for (size_t i = 0; i < kMods.size(); ++i)
        {
            if (name != kMods[i].file)
                continue;
            if (HasBytes(kMods[i], file.bytes, true) != wanted[i])
                SetupLog(Format(L"%s: %s", kMods[i].id, wanted[i] ? L"installed" : L"removed"));
        }
    }

    const ModsStatus after = CheckMods(gameFolder);
    if (!after.ready)
    {
        error = after.details;
        return false;
    }
    for (size_t i = 0; i < kMods.size(); ++i)
    {
        if (after.installed[i] != wanted[i])
        {
            error = Format(L"\"%s\" no ha quedado como se pedía.", kMods[i].name);
            return false;
        }
    }
    return true;
}
