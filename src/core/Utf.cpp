// Utf.cpp - 字符串编码工具实现
#include "core/Utf.h"

#include <windows.h>

#include <cwctype>

namespace backup {

std::string wideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                                  nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(static_cast<size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                          &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

int wcsicmpSafe(const std::wstring& a, const std::wstring& b) {
    // 使用 CompareStringOrdinal 做大小写不敏感比较，对非 ASCII 字符（如中文、德语变音）可靠。
    // 返回值：CSTR_LESS_THAN=1, CSTR_EQUAL=2, CSTR_GREATER_THAN=3
    const int r = ::CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()),
                                          b.c_str(), static_cast<int>(b.size()), TRUE);
    if (r == CSTR_EQUAL) return 0;
    if (r == CSTR_LESS_THAN) return -1;
    return 1;
}

bool startsWithNoCase(const std::wstring& s, const std::wstring& prefix) {
    if (prefix.size() > s.size()) return false;
    return wcsicmpSafe(s.substr(0, prefix.size()), prefix) == 0;
}

}  // namespace backup
