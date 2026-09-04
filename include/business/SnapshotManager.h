// SnapshotManager.h - 快照管理
// 职责：管理备份快照的创建、列表、清理和查询。
//       每次备份创建一个快照目录 snapshots/<timestamp>/，包含该次备份的完整 data/ 和 manifest.txt。
//       未变化的文件用硬链接指向上一个快照（节省空间），变化/新增的文件从源目录复制。
//       保留最近 N 份快照，超过 N 份时自动删除最旧的。
//       支持时间点恢复：从指定快照目录恢复文件。
#pragma once

#include <string>
#include <vector>

namespace backup {

struct SnapshotInfo {
    std::wstring timestamp;  // 快照时间戳 "20260905-000000"
    std::wstring path;       // 快照目录完整路径
};

class SnapshotManager {
public:
    // 获取目标目录下的所有快照（按时间升序，最旧在前）。
    static std::vector<SnapshotInfo> listSnapshots(const std::wstring& targetPath);

    // 创建新快照目录，返回快照信息。时间戳格式 "YYYYMMDD-HHMMSS"。
    static SnapshotInfo createSnapshot(const std::wstring& targetPath);

    // 删除指定快照目录及其所有内容。
    static bool deleteSnapshot(const std::wstring& targetPath, const std::wstring& timestamp);

    // 清理超过 keepCount 份的旧快照，保留最近 keepCount 份。返回删除的快照数。
    static int cleanupOldSnapshots(const std::wstring& targetPath, int keepCount);

    // 获取最新快照（如果有），没有则返回空 timestamp。
    static SnapshotInfo latestSnapshot(const std::wstring& targetPath);

    // 构造快照目录路径：<target>/snapshots/<timestamp>
    static std::wstring snapshotDir(const std::wstring& targetPath, const std::wstring& timestamp);

    // 构造快照的 data 目录路径：<target>/snapshots/<timestamp>/data
    static std::wstring snapshotDataDir(const std::wstring& targetPath, const std::wstring& timestamp);

    // 构造快照的 manifest 路径：<target>/snapshots/<timestamp>/manifest.txt
    static std::wstring snapshotManifestPath(const std::wstring& targetPath, const std::wstring& timestamp);
};

}  // namespace backup
