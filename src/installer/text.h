// Text conversions. The CD's archive stores names in Windows-1252; the installer works in UTF-16 (Windows' own
// encoding) and writes its log in UTF-8.
#pragma once

#include <string>

std::wstring FromCp1252(const std::string& text);
std::wstring FromUtf8(const std::string& text);
std::string ToUtf8(const std::wstring& text);

// Formats like swprintf, for messages.
std::wstring Format(const wchar_t* format, ...);
