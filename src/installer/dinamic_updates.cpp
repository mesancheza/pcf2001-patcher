#include "dinamic_updates.h"

#include "hashes.h"
#include "setup_log.h"
#include "text.h"

#include <windows.h>
#include <fcntl.h>
#include <fdi.h>
#include <io.h>
#include <sys/stat.h>

#include <cstring>
#include <fstream>
#include <map>

namespace
{
bool ReadFileBytes(const std::filesystem::path& path, std::vector<uint8_t>& bytes)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

std::string HexSha256(const std::vector<uint8_t>& bytes)
{
    uint8_t digest[32];
    Sha256(bytes.data(), bytes.size(), digest);
    char hex[65];
    for (int i = 0; i < 32; ++i)
        snprintf(hex + 2 * i, 3, "%02x", digest[i]);
    return hex;
}

// The Microsoft cabinet inside a self-extractor: the first "MSCF" signature with a valid header that fits.
bool FindEmbeddedCabinet(const std::vector<uint8_t>& bytes, size_t& offset, size_t& size)
{
    for (size_t i = 0; i + 36 <= bytes.size(); ++i)
    {
        if (std::memcmp(bytes.data() + i, "MSCF", 4) != 0)
            continue;
        uint32_t reserved, length;
        std::memcpy(&reserved, bytes.data() + i + 4, 4);
        std::memcpy(&length, bytes.data() + i + 8, 4);
        const uint8_t minor = bytes[i + 24], major = bytes[i + 25];
        if (reserved == 0 && major == 1 && minor == 3 && length >= 36 && i + length <= bytes.size())
        {
            offset = i;
            size = length;
            return true;
        }
    }
    return false;
}

// --- Cabinet extraction with Windows' FDI API (cabinet.dll) ---------------------------------------------------------

FNALLOC(FdiAlloc) { return malloc(cb); }
FNFREE(FdiFree) { free(pv); }
FNOPEN(FdiOpen) { return _open(pszFile, oflag | _O_BINARY, pmode); }
FNREAD(FdiRead) { return static_cast<UINT>(_read(static_cast<int>(hf), pv, cb)); }
FNWRITE(FdiWrite) { return static_cast<UINT>(_write(static_cast<int>(hf), pv, cb)); }
FNCLOSE(FdiClose) { return _close(static_cast<int>(hf)); }
FNSEEK(FdiSeek) { return _lseek(static_cast<int>(hf), dist, seektype); }

struct ExtractContext
{
    std::string folder;  // ANSI path with a trailing backslash (FDI works with narrow names)
    int files = 0;
};

FNFDINOTIFY(FdiNotify)
{
    auto* context = static_cast<ExtractContext*>(pfdin->pv);
    switch (fdint)
    {
    case fdintCOPY_FILE:
    {
        const std::string path = context->folder + pfdin->psz1;
        return _open(path.c_str(), _O_BINARY | _O_CREAT | _O_TRUNC | _O_WRONLY, _S_IREAD | _S_IWRITE);
    }
    case fdintCLOSE_FILE_INFO:
        _close(static_cast<int>(pfdin->hf));
        ++context->files;
        return TRUE;
    default:
        return 0;
    }
}

bool ExtractCabinet(const std::filesystem::path& cabinet, const std::filesystem::path& folder, int& files)
{
    ERF erf{};
    HFDI fdi = FDICreate(FdiAlloc, FdiFree, FdiOpen, FdiRead, FdiWrite, FdiClose, FdiSeek, cpuUNKNOWN, &erf);
    if (!fdi)
        return false;
    // FDI takes ANSI paths; the work folder is under the temporary folder, so use its short form to be safe.
    wchar_t shortFolder[MAX_PATH];
    if (!GetShortPathNameW(folder.c_str(), shortFolder, MAX_PATH))
        wcscpy_s(shortFolder, folder.c_str());
    ExtractContext context;
    context.folder = std::filesystem::path(shortFolder).string() + "\\";
    std::string name = cabinet.filename().string();
    std::string path = context.folder;
    const BOOL ok = FDICopy(fdi, name.data(), path.data(), 0, FdiNotify, nullptr, &context);
    FDIDestroy(fdi);
    files = context.files;
    return ok && files > 0;
}

// --- Running Dinamic's updater and answering its dialogs -----------------------------------------------------------

std::wstring WindowText(HWND window)
{
    wchar_t text[1024];
    GetWindowTextW(window, text, 1024);
    return text;
}

std::wstring ClassName(HWND window)
{
    wchar_t name[64];
    GetClassNameW(window, name, 64);
    return name;
}

std::wstring WithoutAmpersand(std::wstring text)
{
    text.erase(std::remove(text.begin(), text.end(), L'&'), text.end());
    return text;
}

struct DialogScan
{
    DWORD processId;
    std::vector<HWND> dialogs;
};

BOOL CALLBACK CollectDialogs(HWND window, LPARAM parameter)
{
    auto* scan = reinterpret_cast<DialogScan*>(parameter);
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId == scan->processId && IsWindowVisible(window) && ClassName(window) == L"#32770")
        scan->dialogs.push_back(window);
    return TRUE;
}

BOOL CALLBACK CollectChildren(HWND window, LPARAM parameter)
{
    reinterpret_cast<std::vector<HWND>*>(parameter)->push_back(window);
    return TRUE;
}

// Runs the updater, presses "Instalar" in its window, and collects the messages it shows (pressing their "OK").
// Returns those messages; the updater reports success with "N ficheros actualizados con éxito".
bool RunUpdater(const std::filesystem::path& updater, std::vector<std::wstring>& messages, std::wstring& error)
{
    STARTUPINFOW startup{sizeof startup};
    PROCESS_INFORMATION process{};
    std::wstring commandLine = L"\"" + updater.native() + L"\"";
    if (!CreateProcessW(updater.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        updater.parent_path().c_str(), &startup, &process))
    {
        error = Format(L"No se puede ejecutar el actualizador de Dinamic (error %lu).", GetLastError());
        return false;
    }
    CloseHandle(process.hThread);

    std::map<HWND, bool> handled;  // dialogs already answered
    bool installPressed = false;
    const DWORD deadline = GetTickCount() + 10 * 60 * 1000;
    while (WaitForSingleObject(process.hProcess, 250) == WAIT_TIMEOUT)
    {
        if (static_cast<int>(GetTickCount() - deadline) > 0)
        {
            TerminateProcess(process.hProcess, 1);
            error = L"El actualizador de Dinamic no ha terminado en 10 minutos.";
            CloseHandle(process.hProcess);
            return false;
        }
        DialogScan scan{process.dwProcessId, {}};
        EnumWindows(CollectDialogs, reinterpret_cast<LPARAM>(&scan));
        for (HWND dialog : scan.dialogs)
        {
            std::vector<HWND> children;
            EnumChildWindows(dialog, CollectChildren, reinterpret_cast<LPARAM>(&children));
            HWND install = nullptr, ok = nullptr;
            std::wstring text;
            for (HWND child : children)
            {
                const std::wstring cls = ClassName(child);
                const std::wstring caption = WithoutAmpersand(WindowText(child));
                if (cls == L"Button" && caption == L"Instalar" && IsWindowEnabled(child))
                    install = child;
                else if (cls == L"Button" && (caption == L"OK" || caption == L"Aceptar") && IsWindowEnabled(child))
                    ok = child;
                else if (cls == L"Static" && !caption.empty())
                    text += (text.empty() ? L"" : L" ") + caption;
            }
            if (install && !installPressed)
            {
                // The updater's main window: start the update.
                SetupLog(L"  updater window: \"" + WindowText(dialog) + L"\"; pressing Instalar");
                PostMessageW(install, BM_CLICK, 0, 0);
                installPressed = true;
            }
            else if (ok && !install && !handled[dialog])
            {
                // A message box: record it and close it.
                SetupLog(L"  updater says: " + text);
                messages.push_back(text);
                handled[dialog] = true;
                PostMessageW(ok, BM_CLICK, 0, 0);
            }
        }
    }
    DWORD exitCode = 0;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);
    SetupLog(Format(L"  updater finished, exit code %lu", exitCode));
    return true;
}
}  // namespace

std::vector<DinamicUpdate> RequiredUpdates()
{
    // The 3.6 update came out between data updates 4 and 5; the data updates check the matchday they start from.
    return {
        {L"PCFACT01.exe", L"Actualización de datos 1", 597504, "43bca94dd794ac420f954efe4ce6412919324c25e8a5cc8b8f0a9ed00832e1f8", {}},
        {L"PCFACT02.exe", L"Actualización de datos 2", 635392, "593093aa20da3dcad9a190f32955fac28f4d9238813ea2b1af59f5b9bbccf040", {}},
        {L"PCFACT03.exe", L"Actualización de datos 3", 572416, "14bcc0a58fc2068f77ad5877a094b280c0de3a103e77deea8ae476a280d5e2a5", {}},
        {L"PCFACT04.exe", L"Actualización de datos 4", 612352, "289e0ced690f7660027279c79dd6a142c2f6be1ffc3c65d35cf62fdacc3a76a7", {}},
        {L"PCFPATCH3.6.EXE", L"Actualización a la versión 3.6", 1434112, "413a5290d561ea054b0e3f6e5011a8314c2bee35c4279c6795a7f3033bed42a1", {}},
        {L"PCFACT05.exe", L"Actualización de datos 5", 541184, "2e6124a84964be12b9d0b103d073ec7226fe0ec9fbd4bd2a59c06f0a4a55a30b", {}},
        {L"PCFACT06.exe", L"Actualización de datos 6", 462336, "3a2c13054117546da09345f8a63fdc43a5fcdc71ff0bea8f93c9b5093068c5db", {}},
        {L"PCFACT07.exe", L"Actualización de datos 7", 446464, "0489d8520cf7679db5ba5d9dfd710a816ccc6c5a102fa28081fe5a08c87e46fb", {}},
        {L"PCFACT08.exe", L"Actualización de datos 8", 468480, "349b8c1e209e8aa6d855d673ac3f1de2d05ab0763901457ab5151e2ca5e66414", {}},
        {L"PCFACT09.exe", L"Actualización de datos 9", 519680, "aa28a8801355f48c6fb376d908eb8c1d7035eb7391ce2a38f3b227cd285fc875", {}},
        {L"PCFACT10.exe", L"Actualización de datos 10", 536576, "a84ca6cf00aab54b73b703371e1c81d81e9ab2c56d1c90e5f60f8f98a6929cd3", {}},
        {L"PCFACT11.exe", L"Actualización de datos 11", 519168, "98372c60e6f5b461c49e66e5ae012b3c729fb925d1d80146876776d49e8a6faa", {}},
        {L"PCFACT12.exe", L"Actualización de datos 12", 707584, "870ed1486f3b15bcb6d7d10bf06d8decde531c1760fb43140c8c94fad35e8852", {}},
    };
}

std::wstring FindUpdates(const std::filesystem::path& folder, std::vector<DinamicUpdate>& updates)
{
    updates = RequiredUpdates();
    for (auto& update : updates)
    {
        const auto path = folder / update.fileName;  // Windows file names ignore case
        std::error_code fsError;
        if (!std::filesystem::exists(path, fsError))
            return Format(L"Falta %s en la carpeta de actualizaciones.", update.fileName);
        std::vector<uint8_t> bytes;
        if (!ReadFileBytes(path, bytes))
            return Format(L"No se puede leer %s.", update.fileName);
        if (bytes.size() != update.size || HexSha256(bytes) != update.sha256)
            return Format(L"%s no es el archivo original de Dinamic (está dañado o modificado).", update.fileName);
        update.path = path;
    }
    return {};
}

bool ApplyUpdate(const DinamicUpdate& update, const std::filesystem::path& workFolder, std::wstring& error)
{
    SetupLog(Format(L"update %s (%s)", update.fileName, update.description));
    std::vector<uint8_t> bytes;
    size_t offset = 0, size = 0;
    if (!ReadFileBytes(update.path, bytes) || !FindEmbeddedCabinet(bytes, offset, size))
    {
        error = Format(L"No se puede abrir %s.", update.fileName);
        return false;
    }
    std::error_code fsError;
    std::filesystem::create_directories(workFolder, fsError);
    const auto cabinet = workFolder / L"update.cab";
    {
        std::ofstream out(cabinet, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data() + offset), static_cast<std::streamsize>(size));
    }
    int files = 0;
    if (!ExtractCabinet(cabinet, workFolder, files))
    {
        error = Format(L"No se puede desempaquetar %s.", update.fileName);
        return false;
    }
    SetupLog(Format(L"  unpacked %d files", files));

    std::vector<std::wstring> messages;
    if (!RunUpdater(workFolder / L"PCFPATCH.exe", messages, error))
        return false;
    for (const auto& message : messages)
    {
        if (message.find(L"con éxito") != std::wstring::npos)
            return true;
    }
    error = Format(L"%s no se ha aplicado.", update.description);
    if (!messages.empty())
        error += L" El actualizador de Dinamic dice: \"" + messages.back() + L"\"";
    return false;
}
