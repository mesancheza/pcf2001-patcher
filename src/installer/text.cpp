#include "text.h"

#include <windows.h>

#include <cstdarg>
#include <cwchar>
#include <vector>

namespace
{
std::wstring ToWide(UINT codePage, const std::string& text)
{
    if (text.empty())
        return {};
    const int length = MultiByteToWideChar(codePage, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(codePage, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return wide;
}
}  // namespace

std::wstring FromCp1252(const std::string& text)
{
    return ToWide(1252, text);
}

std::wstring FromUtf8(const std::string& text)
{
    return ToWide(CP_UTF8, text);
}

std::string ToUtf8(const std::wstring& text)
{
    if (text.empty())
        return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), length, nullptr, nullptr);
    return utf8;
}

std::wstring Format(const wchar_t* format, ...)
{
    va_list args;
    va_start(args, format);
    va_list copy;
    va_copy(copy, args);
    const int length = _vscwprintf(format, copy);
    va_end(copy);
    std::vector<wchar_t> buffer(static_cast<size_t>(length) + 1);
    vswprintf_s(buffer.data(), buffer.size(), format, args);
    va_end(args);
    return std::wstring(buffer.data(), static_cast<size_t>(length));
}
