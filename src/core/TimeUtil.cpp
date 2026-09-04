// TimeUtil.cpp - 时间格式化实现
#include "core/TimeUtil.h"

#include <windows.h>

#include <cstdio>

namespace backup {

namespace {

SYSTEMTIME localNow() {
    SYSTEMTIME st{};
    ::GetLocalTime(&st);
    return st;
}

}  // namespace

std::string formatNowUtf8() {
    const SYSTEMTIME st = localNow();
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u",
                  st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

std::wstring formatNowWide() {
    const std::string s = formatNowUtf8();
    std::wstring w(s.size(), L'\0');
    for (size_t i = 0; i < s.size(); ++i) w[i] = static_cast<wchar_t>(static_cast<unsigned char>(s[i]));
    return w;
}

std::string makeBackupId() {
    const SYSTEMTIME st = localNow();
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04u%02u%02u-%02u%02u%02u",
                  st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

std::string dateToday() {
    const SYSTEMTIME st = localNow();
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04u-%02u-%02u", st.wYear, st.wMonth, st.wDay);
    return buf;
}

std::string currentHHMM() {
    const SYSTEMTIME st = localNow();
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02u:%02u", st.wHour, st.wMinute);
    return buf;
}

bool parseHHMM(const std::wstring& s, int& hour, int& minute) {
    if (s.size() != 5 || s[2] != L':') return false;
    for (size_t i = 0; i < s.size(); ++i) {
        if (i == 2) continue;
        if (s[i] < L'0' || s[i] > L'9') return false;
    }
    hour = (s[0] - L'0') * 10 + (s[1] - L'0');
    minute = (s[3] - L'0') * 10 + (s[4] - L'0');
    return hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59;
}

}  // namespace backup
