// SnapshotManager.cpp - 快照管理实现
#include "business/SnapshotManager.h"

#include <algorithm>

#include <windows.h>

#include "core/TimeUtil.h"
#include "core/Utf.h"
#include "engine/FileSystem.h"

namespace backup {

std::wstring SnapshotManager::snapshotDir(const std::wstring& targetPath, const std::wstring& timestamp) {
    return targetPath + L"\\snapshots\\" + timestamp;
}

std::wstring SnapshotManager::snapshotDataDir(const std::wstring& targetPath, const std::wstring& timestamp) {
    return snapshotDir(targetPath, timestamp) + L"\\data";
}

std::wstring SnapshotManager::snapshotManifestPath(const std::wstring& targetPath, const std::wstring& timestamp) {
    return snapshotDir(targetPath, timestamp) + L"\\manifest.txt";
}

std::vector<SnapshotInfo> SnapshotManager::listSnapshots(const std::wstring& targetPath) {
    std::vector<SnapshotInfo> result;
    const std::wstring snapshotsRoot = targetPath + L"\\snapshots";
    if (!FileSystem::exists(snapshotsRoot)) return result;

    WIN32_FIND_DATAW fd;
    const std::wstring searchPattern = snapshotsRoot + L"\\*";
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return result;

    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        const std::wstring name(fd.cFileName);
        if (name == L"." || name == L"..") continue;
        // 快照目录名应为时间戳格式 "YYYYMMDD-HHMMSS"（15 字符），
        // 或同秒内多次备份的带序号格式 "YYYYMMDD-HHMMSS-N"（N 为数字）。
        if (name.size() < 15 || name[8] != L'-') continue;
        bool valid = true;
        for (size_t i = 0; i < 15; ++i) {
            if (i == 8) continue;
            if (name[i] < L'0' || name[i] > L'9') { valid = false; break; }
        }
        if (!valid) continue;
        // 检查可选的序号后缀 "-N"
        if (name.size() > 15) {
            if (name[15] != L'-') continue;
            for (size_t i = 16; i < name.size(); ++i) {
                if (name[i] < L'0' || name[i] > L'9') { valid = false; break; }
            }
            if (!valid) continue;
        }

        SnapshotInfo info;
        info.timestamp = name;
        info.path = snapshotsRoot + L"\\" + name;
        result.push_back(info);
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    // 按时间戳升序（最旧在前）
    std::sort(result.begin(), result.end(),
              [](const SnapshotInfo& a, const SnapshotInfo& b) { return a.timestamp < b.timestamp; });
    return result;
}

SnapshotInfo SnapshotManager::createSnapshot(const std::wstring& targetPath) {
    // 生成时间戳 "YYYYMMDD-HHMMSS"，如果已存在则追加序号
    std::wstring timestamp = utf8ToWide(makeBackupId());  // "YYYYMMDD-HHMMSS"
    std::wstring dir = snapshotDir(targetPath, timestamp);
    int suffix = 1;
    while (FileSystem::exists(dir)) {
        timestamp = utf8ToWide(makeBackupId()) + L"-" + std::to_wstring(suffix);
        dir = snapshotDir(targetPath, timestamp);
        ++suffix;
    }

    FileSystem::createDirectories(dir + L"\\data");

    SnapshotInfo info;
    info.timestamp = timestamp;
    info.path = dir;
    return info;
}

bool SnapshotManager::deleteSnapshot(const std::wstring& targetPath, const std::wstring& timestamp) {
    const std::wstring dir = snapshotDir(targetPath, timestamp);
    if (!FileSystem::exists(dir)) return true;  // 不存在视为成功（幂等）
    return FileSystem::removeAll(dir);
}

int SnapshotManager::cleanupOldSnapshots(const std::wstring& targetPath, int keepCount) {
    if (keepCount <= 0) return 0;
    std::vector<SnapshotInfo> snapshots = listSnapshots(targetPath);
    if (static_cast<int>(snapshots.size()) <= keepCount) return 0;

    const int toDelete = static_cast<int>(snapshots.size()) - keepCount;
    int deleted = 0;
    for (int i = 0; i < toDelete; ++i) {
        if (deleteSnapshot(targetPath, snapshots[static_cast<size_t>(i)].timestamp)) {
            ++deleted;
        }
    }
    return deleted;
}

SnapshotInfo SnapshotManager::latestSnapshot(const std::wstring& targetPath) {
    std::vector<SnapshotInfo> snapshots = listSnapshots(targetPath);
    if (snapshots.empty()) return SnapshotInfo{};
    return snapshots.back();
}

}  // namespace backup
