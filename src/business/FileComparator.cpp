// FileComparator.cpp - 文件变更比较实现
#include "business/FileComparator.h"

#include <algorithm>
#include <set>

#include "core/Utf.h"

namespace backup {

std::vector<ChangeRecord> FileComparator::compare(const std::vector<FileInfo>& current,
                                                  const Manifest& previous,
                                                  const HashProvider& hashProvider) {
    std::vector<ChangeRecord> result;
    result.reserve(current.size() + previous.entries.size());

    // 旧状态索引：relativePath -> Entry
    std::map<std::wstring, const Manifest::Entry*> prevMap;
    for (const auto& e : previous.entries) {
        prevMap[e.info.relativePath] = &e;
    }

    std::set<std::wstring> seen;
    // 类型互换时旧条目需要单独产生一条 Deleted 记录，
    // 让上层清理旧数据（文件变目录时删旧文件；目录变文件时删旧目录树）。
    const auto pushDeleted = [](std::vector<ChangeRecord>& out, const Manifest::Entry& old) {
        ChangeRecord rec;
        rec.info = old.info;
        rec.info.hash.clear();
        rec.info.hashed = false;
        rec.change = FileChangeType::Deleted;
        out.push_back(std::move(rec));
    };

    for (const auto& fi : current) {
        seen.insert(fi.relativePath);
        ChangeRecord rec;
        rec.info = fi;

        const auto it = prevMap.find(fi.relativePath);
        if (it == prevMap.end()) {
            // 无历史记录 -> 新增（目录也标记 Added，进入新 Manifest 以保留空目录结构）
            rec.change = FileChangeType::Added;
        } else {
            const Manifest::Entry& old = *it->second;
            if (fi.type == FileType::Directory) {
                if (old.info.type == FileType::Directory) {
                    // 目录前后都是目录 -> 未变化
                    rec.change = FileChangeType::Unchanged;
                } else {
                    // 文件 -> 目录：当前目录重新入库，旧文件删除
                    rec.change = FileChangeType::Added;
                    pushDeleted(result, *it->second);
                }
            } else if (old.info.type != FileType::File) {
                // 目录/符号链接 -> 文件：当前文件重新备份，旧对象删除
                rec.change = FileChangeType::Added;
                pushDeleted(result, *it->second);
            } else if (fi.size != old.info.size) {
                // 大小变化 -> 修改
                rec.change = FileChangeType::Modified;
            } else if (fi.modifiedTime == old.info.modifiedTime) {
                // 秒级时间相同，再比较 100ns 精度时间（防止同秒内内容变化漏报）
                if (fi.modifiedTime100ns != 0 && old.info.modifiedTime100ns != 0 &&
                    fi.modifiedTime100ns != old.info.modifiedTime100ns) {
                    rec.change = FileChangeType::Modified;
                } else {
                    rec.change = FileChangeType::Unchanged;
                }
            } else {
                // 大小相同但修改时间变化 -> Hash 二次确认
                FileInfo withHash = fi;
                std::string newHash;
                if (hashProvider && hashProvider(fi, newHash)) {
                    rec.change = (!newHash.empty() && newHash == old.info.hash)
                                     ? FileChangeType::Unchanged
                                     : FileChangeType::Modified;
                    withHash.hash = newHash;
                    withHash.hashed = !newHash.empty();
                } else {
                    // 无法计算 Hash -> 保守视为修改，确保数据被重新备份
                    rec.change = FileChangeType::Modified;
                }
                rec.info = std::move(withHash);
            }
        }
        result.push_back(std::move(rec));
    }

    // 上次存在、本次已删除的条目。
    for (const auto& e : previous.entries) {
        if (seen.count(e.info.relativePath) == 0) {
            ChangeRecord rec;
            rec.info = e.info;
            rec.info.hash.clear();
            rec.info.hashed = false;
            rec.change = FileChangeType::Deleted;
            result.push_back(std::move(rec));
        }
    }

    std::sort(result.begin(), result.end(),
              [](const ChangeRecord& a, const ChangeRecord& b) {
                  return wcsicmpSafe(a.info.relativePath, b.info.relativePath) < 0;
              });
    return result;
}

}  // namespace backup
