// FileSystem.cpp - Windows 文件系统操作实现
#include "engine/FileSystem.h"

#include <cwctype>

#include "core/Utf.h"

namespace backup {

namespace {

// FILETIME(1601 起 100ns) -> Unix 秒
uint64_t filetimeToUnixSeconds(const FILETIME& ft) {
    ULARGE_INTEGER ul;
    ul.LowPart = ft.dwLowDateTime;
    ul.HighPart = ft.dwHighDateTime;
    constexpr uint64_t kEpochDiff100ns = 116444736000000000ULL;  // 1601->1970, 单位 100ns
    if (ul.QuadPart < kEpochDiff100ns) return 0;
    return (ul.QuadPart - kEpochDiff100ns) / 10000000ULL;
}

// Unix 秒 -> FILETIME
FILETIME unixSecondsToFiletime(uint64_t sec) {
    ULARGE_INTEGER ul;
    ul.QuadPart = (sec * 10000000ULL) + 116444736000000000ULL;
    FILETIME ft;
    ft.dwLowDateTime = ul.LowPart;
    ft.dwHighDateTime = ul.HighPart;
    return ft;
}

FileType attributesToType(DWORD attrs) {
    // reparse point（junction/符号链接）必须优先判断，
    // 否则带 DIRECTORY 属性的 junction 会被当成普通目录递归，可能成环或越界备份。
    if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) return FileType::Symlink;
    if (attrs & FILE_ATTRIBUTE_DIRECTORY) return FileType::Directory;
    return FileType::File;
}

bool isDriveLetterPrefix(const std::wstring& p) {
    // 仅当 p 本身就是盘符根（"C:" 或 "C:\"）时返回 true，
    // 完整路径（如 "C:\data"）不得误判为盘符根。
    if (p.size() < 2) return false;
    const wchar_t c0 = p[0];
    if (!((c0 >= L'A' && c0 <= L'Z') || (c0 >= L'a' && c0 <= L'z'))) return false;
    if (p[1] != L':') return false;
    return p.size() == 2 || (p.size() == 3 && (p[2] == L'\\' || p[2] == L'/'));
}

}  // namespace

bool FileSystem::exists(const std::wstring& path) {
    if (path.empty()) return false;
    const DWORD attrs = ::GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES;
}

bool FileSystem::isDirectory(const std::wstring& path) {
    const DWORD attrs = ::GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;
    return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool FileSystem::createDirectories(const std::wstring& path) {
    if (path.empty()) return false;
    const DWORD attrs = ::GetFileAttributesW(path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) {
        return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }
    // 递归创建父目录；盘符前缀（C: / C:\）视为根，不再向上。
    const size_t pos = path.find_last_of(L"\\/");
    const std::wstring parent = (pos == std::wstring::npos) ? L"" : path.substr(0, pos);
    if (!parent.empty() && !isDriveLetterPrefix(parent)) {
        if (!createDirectories(parent)) return false;
    }
    if (::CreateDirectoryW(path.c_str(), nullptr)) return true;
    const DWORD err = ::GetLastError();
    if (err == ERROR_ALREADY_EXISTS) {
        const DWORD a = ::GetFileAttributesW(path.c_str());
        return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }
    return false;
}

bool FileSystem::listDirectory(const std::wstring& dirPath,
                               std::vector<std::pair<std::wstring, FileType>>& out) {
    out.clear();
    const std::wstring pattern = dirPath + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE h = ::FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool ok = true;
    do {
        const std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        out.emplace_back(name, attributesToType(fd.dwFileAttributes));
    } while (::FindNextFileW(h, &fd));
    const DWORD err = ::GetLastError();
    ::FindClose(h);
    if (err != ERROR_NO_MORE_FILES) ok = false;
    return ok;
}

bool FileSystem::getFileInfo(const std::wstring& path, FileInfo& out) {
    WIN32_FIND_DATAW fd;
    HANDLE h = ::FindFirstFileW(path.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    ::FindClose(h);

    out.name = fd.cFileName;
    out.size = (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
    out.createdTime = filetimeToUnixSeconds(fd.ftCreationTime);
    out.modifiedTime = filetimeToUnixSeconds(fd.ftLastWriteTime);
    // 100ns 精度修改时间（FILETIME 本身就是 100ns 单位，从 1601 年起）
    {
        ULARGE_INTEGER ul;
        ul.LowPart = fd.ftLastWriteTime.dwLowDateTime;
        ul.HighPart = fd.ftLastWriteTime.dwHighDateTime;
        out.modifiedTime100ns = ul.QuadPart;
    }
    out.type = attributesToType(fd.dwFileAttributes);
    return true;
}

FileHandle FileSystem::openRead(const std::wstring& path, std::string* err) {
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        if (err) *err = std::string("open for read failed: ") + wideToUtf8(errorString(::GetLastError()));
        return FileHandle();
    }
    return FileHandle(h);
}

FileHandle FileSystem::openWrite(const std::wstring& path, bool overwrite, std::string* err) {
    const DWORD disposition = overwrite ? CREATE_ALWAYS : CREATE_NEW;
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                             nullptr, disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        if (err) *err = std::string("open for write failed: ") + wideToUtf8(errorString(::GetLastError()));
        return FileHandle();
    }
    return FileHandle(h);
}

bool FileSystem::read(HANDLE h, void* buf, DWORD n, DWORD& readCount, std::string* err) {
    readCount = 0;
    if (!::ReadFile(h, buf, n, &readCount, nullptr)) {
        if (err) *err = std::string("read failed: ") + wideToUtf8(errorString(::GetLastError()));
        return false;
    }
    return true;
}

bool FileSystem::write(HANDLE h, const void* buf, DWORD n, DWORD& writtenCount, std::string* err) {
    writtenCount = 0;
    if (!::WriteFile(h, buf, n, &writtenCount, nullptr)) {
        if (err) *err = std::string("write failed: ") + wideToUtf8(errorString(::GetLastError()));
        return false;
    }
    return true;
}

bool FileSystem::setFileTimes(const std::wstring& path, uint64_t createdSec, uint64_t modifiedSec) {
    HANDLE h = ::CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES,
                             FILE_SHARE_READ | FILE_SHARE_WRITE,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    FILETIME ct{}, mt{};
    if (createdSec != 0) ct = unixSecondsToFiletime(createdSec);
    if (modifiedSec != 0) mt = unixSecondsToFiletime(modifiedSec);
    const BOOL ok = ::SetFileTime(h, createdSec ? &ct : nullptr, nullptr, modifiedSec ? &mt : nullptr);
    ::CloseHandle(h);
    return ok != FALSE;
}

bool FileSystem::deleteFile(const std::wstring& path) {
    return ::DeleteFileW(path.c_str()) != FALSE;
}

uint64_t FileSystem::nowSeconds() {
    FILETIME ft;
    ::GetSystemTimeAsFileTime(&ft);
    return filetimeToUnixSeconds(ft);
}

std::wstring FileSystem::errorString(DWORD err) {
    wchar_t* buf = nullptr;
    const DWORD n = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err, 0, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::wstring s = (n > 0 && buf) ? std::wstring(buf, n) : L"";
    if (buf) ::LocalFree(buf);
    while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n' || s.back() == L' ')) s.pop_back();
    return s;
}

}  // namespace backup
