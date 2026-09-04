// TestUtil.h - 测试辅助：临时目录、写文件
#pragma once

#include <windows.h>

#include <fstream>
#include <random>
#include <string>

#include "engine/FileSystem.h"

namespace testutil {

// 在系统临时目录下创建唯一子目录。
inline std::wstring makeTempDir(const std::wstring& tag) {
    wchar_t buf[MAX_PATH];
    ::GetTempPathW(MAX_PATH, buf);
    std::random_device rd;
    const auto id = static_cast<unsigned long long>(::GetCurrentProcessId()) ^
                    (static_cast<unsigned long long>(rd()) << 32) ^ rd();
    std::wstring dir = std::wstring(buf) + L"backup_test_" + tag + L"_" +
                       std::to_wstring(id) + L"\\";
    backup::FileSystem::createDirectories(dir);
    return dir;
}

inline void writeFile(const std::wstring& path, const std::string& content) {
    const size_t pos = path.find_last_of(L"\\/");
    if (pos != std::wstring::npos) backup::FileSystem::createDirectories(path.substr(0, pos));
    std::ofstream ofs(path.c_str(), std::ios::binary);
    ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
    ofs.flush();
}

inline void removeAll(const std::wstring& path) {
    if (!backup::FileSystem::exists(path)) return;
    if (backup::FileSystem::isDirectory(path)) {
        std::vector<std::pair<std::wstring, backup::FileType>> entries;
        backup::FileSystem::listDirectory(path, entries);
        for (const auto& e : entries) removeAll(path + L"\\" + e.first);
        ::RemoveDirectoryW(path.c_str());
    } else {
        backup::FileSystem::deleteFile(path);
    }
}

}  // namespace testutil
