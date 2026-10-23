#include "system_integration.h"

#include "setup_log.h"
#include "text.h"

#include <windows.h>
#include <aclapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <objbase.h>

namespace
{
const wchar_t* const kGameKey = L"SOFTWARE\\DINAMIC MULTIMEDIA\\PC FÚTBOL 2001";
const wchar_t* const kVendorKey = L"SOFTWARE\\DINAMIC MULTIMEDIA";
const wchar_t* const kUninstallKey = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\PCF2001";
constexpr REGSAM kView32 = KEY_WOW64_32KEY;

std::filesystem::path KnownFolder(REFKNOWNFOLDERID id)
{
    PWSTR path = nullptr;
    std::filesystem::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &path)))
        result = path;
    CoTaskMemFree(path);
    return result;
}

std::filesystem::path StartMenuFolder()
{
    return KnownFolder(FOLDERID_CommonPrograms) / kGameName;
}

std::filesystem::path DesktopShortcut()
{
    return KnownFolder(FOLDERID_PublicDesktop) / (std::wstring(kGameName) + L".lnk");
}

bool CreateLink(const std::filesystem::path& link, const std::filesystem::path& target, const std::filesystem::path& workingFolder,
                const std::wstring& description, const std::filesystem::path& icon)
{
    IShellLinkW* shellLink = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shellLink))))
        return false;
    shellLink->SetPath(target.c_str());
    shellLink->SetWorkingDirectory(workingFolder.c_str());
    shellLink->SetDescription(description.c_str());
    if (!icon.empty())
        shellLink->SetIconLocation(icon.c_str(), 0);
    IPersistFile* file = nullptr;
    HRESULT hr = shellLink->QueryInterface(IID_PPV_ARGS(&file));
    if (SUCCEEDED(hr))
    {
        hr = file->Save(link.c_str(), TRUE);
        file->Release();
    }
    shellLink->Release();
    return SUCCEEDED(hr);
}

bool SetString(HKEY key, const wchar_t* name, const std::wstring& value)
{
    return RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                          static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

bool SetDword(HKEY key, const wchar_t* name, DWORD value)
{
    return RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof value) == ERROR_SUCCESS;
}
}  // namespace

bool WriteGameFolderKey(const std::filesystem::path& gameFolder)
{
    HKEY key;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, kGameKey, 0, nullptr, 0, KEY_SET_VALUE | kView32, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return false;
    const bool ok = SetString(key, L"DIR", gameFolder.native());
    RegCloseKey(key);
    SetupLog(ok ? L"registry: DIR written" : L"registry: cannot write DIR");
    return ok;
}

std::filesystem::path ReadGameFolderKey()
{
    wchar_t value[MAX_PATH];
    DWORD size = sizeof value;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, kGameKey, L"DIR", RRF_RT_REG_SZ | RRF_SUBKEY_WOW6432KEY, nullptr, value, &size) != ERROR_SUCCESS)
        return {};
    return value;
}

void DeleteGameFolderKey()
{
    RegDeleteKeyExW(HKEY_LOCAL_MACHINE, kGameKey, kView32, 0);
    // The vendor key goes too, unless another Dinamic game still uses it.
    RegDeleteKeyExW(HKEY_LOCAL_MACHINE, kVendorKey, kView32, 0);
}

bool GrantUsersModify(const std::filesystem::path& folder)
{
    // Built-in "Users" group, by its well-known identifier so it works in every Windows language.
    PSID users = nullptr;
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    if (!AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_USERS, 0, 0, 0, 0, 0, 0, &users))
        return false;
    PACL current = nullptr, updated = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    bool ok = GetNamedSecurityInfoW(folder.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &current,
                                    nullptr, &descriptor) == ERROR_SUCCESS;
    if (ok)
    {
        EXPLICIT_ACCESSW access{};
        access.grfAccessPermissions = FILE_GENERIC_READ | FILE_GENERIC_WRITE | FILE_GENERIC_EXECUTE | DELETE;
        access.grfAccessMode = GRANT_ACCESS;
        access.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
        access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        access.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
        access.Trustee.ptstrName = static_cast<LPWSTR>(users);
        ok = SetEntriesInAclW(1, &access, current, &updated) == ERROR_SUCCESS &&
             SetNamedSecurityInfoW(const_cast<LPWSTR>(folder.c_str()), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr,
                                   updated, nullptr) == ERROR_SUCCESS;
    }
    if (updated)
        LocalFree(updated);
    if (descriptor)
        LocalFree(descriptor);
    FreeSid(users);
    SetupLog(ok ? L"permissions: users can modify the game folder" : L"permissions: cannot change the game folder's permissions");
    return ok;
}

bool CreateShortcuts(const std::filesystem::path& gameFolder, const std::filesystem::path& uninstaller, bool desktop)
{
    const auto menu = StartMenuFolder();
    std::error_code error;
    std::filesystem::create_directories(menu, error);
    // Dinamic's launcher: checks for the CD, offers the intro video and starts the game.
    const auto launcher = gameFolder / L"pcf2001.exe";
    const auto icon = gameFolder / L"PCF2001.ico";
    bool ok = CreateLink(menu / (std::wstring(kGameName) + L".lnk"), launcher, gameFolder, kGameName, icon);
    ok = CreateLink(menu / L"Desinstalar.lnk", uninstaller, gameFolder, L"Desinstalar PC FÚTBOL 2001", {}) && ok;
    if (desktop)
        ok = CreateLink(DesktopShortcut(), launcher, gameFolder, kGameName, icon) && ok;
    SetupLog(ok ? L"shortcuts created" : L"some shortcuts could not be created");
    return ok;
}

void DeleteShortcuts()
{
    std::error_code error;
    std::filesystem::remove_all(StartMenuFolder(), error);
    std::filesystem::remove(DesktopShortcut(), error);
}

bool RegisterUninstaller(const std::filesystem::path& gameFolder, const std::filesystem::path& uninstaller, unsigned long long sizeBytes)
{
    HKEY key;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, kUninstallKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return false;
    bool ok = SetString(key, L"DisplayName", kGameName);
    ok = SetString(key, L"Publisher", L"Dinamic Multimedia") && ok;
    ok = SetString(key, L"DisplayVersion", L"3.6") && ok;
    ok = SetString(key, L"DisplayIcon", (gameFolder / L"pcf2001.exe").native()) && ok;
    ok = SetString(key, L"InstallLocation", gameFolder.native()) && ok;
    ok = SetString(key, L"UninstallString", L"\"" + uninstaller.native() + L"\" /desinstalar") && ok;
    ok = SetString(key, L"Comments", L"Instalado con pcf2001-setup (proyecto no oficial)") && ok;
    ok = SetDword(key, L"NoModify", 1) && ok;
    ok = SetDword(key, L"NoRepair", 1) && ok;
    ok = SetDword(key, L"EstimatedSize", static_cast<DWORD>(sizeBytes / 1024)) && ok;
    RegCloseKey(key);
    SetupLog(ok ? L"registered in Windows' list of installed programs" : L"cannot register in Windows' list of installed programs");
    return ok;
}

void UnregisterUninstaller()
{
    RegDeleteKeyExW(HKEY_LOCAL_MACHINE, kUninstallKey, KEY_WOW64_64KEY, 0);
    RegDeleteKeyExW(HKEY_LOCAL_MACHINE, kUninstallKey, kView32, 0);
}
