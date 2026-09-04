// Manifest.cpp - 备份清单实现
#include "business/Manifest.h"

#include <windows.h>

#include <fstream>
#include <sstream>

#include "core/Utf.h"

namespace backup {

void Manifest::rebuildIndex() {
    index_.clear();
    for (size_t i = 0; i < entries.size(); ++i) {
        index_[entries[i].info.relativePath] = i;
    }
}

const Manifest::Entry* Manifest::find(const std::wstring& relativePath) const {
    const auto it = index_.find(relativePath);
    if (it == index_.end()) return nullptr;
    return &entries[it->second];
}

bool Manifest::saveToFile(const std::wstring& path, std::string* err) const {
    // 原子写入：先写同目录临时文件，成功后 MoveFileEx 替换原文件，
    // 避免写入中途断电/崩溃导致原 Manifest 被截断损坏。
    const std::wstring tmp = path + L".tmp";
    ::DeleteFileW(tmp.c_str());

    std::ofstream ofs(tmp.c_str(), std::ios::binary | std::ios::trunc);
    if (!ofs) {
        if (err) *err = "cannot open manifest for write";
        return false;
    }
    std::string body;
    body += "# BackupSystem Manifest\n";
    body += "version=" + meta.formatVersion + "\n";
    body += "backup_id=" + meta.backupId + "\n";
    body += "source=" + wideToUtf8(meta.sourcePath) + "\n";
    body += "created=" + meta.created + "\n";
    body += "type=" + meta.backupType + "\n";
    body += "file_count=" + std::to_string(meta.fileCount) + "\n";
    if (!meta.encryption.empty() && meta.encryption != "none") {
        body += "encryption=" + meta.encryption + "\n";
    }
    for (const auto& e : entries) {
        body += "\n[file]\n";
        body += "path=" + wideToUtf8(e.info.relativePath) + "\n";
        body += "type=" + std::to_string(static_cast<int>(e.info.type)) + "\n";
        body += "size=" + std::to_string(e.info.size) + "\n";
        body += "mtime=" + std::to_string(e.info.modifiedTime) + "\n";
        body += "mtime_100ns=" + std::to_string(e.info.modifiedTime100ns) + "\n";
        body += "hash=" + e.info.hash + "\n";
        body += "data=" + wideToUtf8(e.dataPath) + "\n";
    }
    ofs.write(body.data(), static_cast<std::streamsize>(body.size()));
    ofs.flush();
    if (!ofs) {
        if (err) *err = "write manifest failed";
        ::DeleteFileW(tmp.c_str());
        return false;
    }
    ofs.close();

    // 原子替换：同卷 MoveFileEx 只更新目录项，不会留下中间状态
    if (!::MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        const DWORD e = ::GetLastError();
        if (err) *err = "atomic replace failed (error " + std::to_string(e) + ")";
        ::DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

bool Manifest::loadFromFile(const std::wstring& path, std::string* err) {
    std::ifstream ifs(path.c_str(), std::ios::binary);
    if (!ifs) {
        if (err) *err = std::string("cannot open manifest: ") + wideToUtf8(path);
        return false;
    }
    meta = Meta();
    entries.clear();
    index_.clear();

    // 校验文件头：第一行必须是标识行，防止把任意文本当成合法清单
    std::string firstLine;
    if (!std::getline(ifs, firstLine)) {
        if (err) *err = "manifest is empty";
        return false;
    }
    if (!firstLine.empty() && firstLine.back() == '\r') firstLine.pop_back();
    if (firstLine != "# BackupSystem Manifest") {
        if (err) *err = "invalid manifest header: " + firstLine;
        return false;
    }

    // 安全数值解析：失败时返回 false，避免 stoull 抛出未捕获异常
    bool parseError = false;
    std::string parseErrMsg;
    const auto safeStoull = [&](const std::string& v, const char* field) -> uint64_t {
        try { return std::stoull(v); }
        catch (...) { parseError = true; parseErrMsg = std::string("invalid ") + field + ": " + v; return 0; }
    };
    const auto safeStoi = [&](const std::string& v, const char* field) -> int {
        try { return std::stoi(v); }
        catch (...) { parseError = true; parseErrMsg = std::string("invalid ") + field + ": " + v; return 0; }
    };

    std::string line;
    bool inFile = false;
    bool haveEntry = false;
    Entry cur;
    const auto flushEntry = [&]() {
        if (haveEntry) {
            entries.push_back(std::move(cur));
            cur = Entry();
            haveEntry = false;
        }
    };

    while (std::getline(ifs, line)) {
        if (parseError) break;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == "[file]") {
            flushEntry();
            inFile = true;
            haveEntry = true;
            continue;
        }
        if (line.empty()) continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string val = line.substr(eq + 1);

        if (!inFile) {
            if (key == "version") meta.formatVersion = val;
            else if (key == "backup_id") meta.backupId = val;
            else if (key == "source") meta.sourcePath = utf8ToWide(val);
            else if (key == "created") meta.created = val;
            else if (key == "type") meta.backupType = val;
            else if (key == "file_count") meta.fileCount = safeStoull(val, "file_count");
            else if (key == "encryption") meta.encryption = val;
        } else {
            if (key == "path") cur.info.relativePath = utf8ToWide(val);
            else if (key == "type") cur.info.type = static_cast<FileType>(safeStoi(val, "type"));
            else if (key == "size") cur.info.size = safeStoull(val, "size");
            else if (key == "mtime") cur.info.modifiedTime = safeStoull(val, "mtime");
            else if (key == "mtime_100ns") cur.info.modifiedTime100ns = safeStoull(val, "mtime_100ns");
            else if (key == "hash") { cur.info.hash = val; cur.info.hashed = !val.empty(); }
            else if (key == "data") cur.dataPath = utf8ToWide(val);
        }
    }
    flushEntry();

    if (parseError) {
        if (err) *err = parseErrMsg;
        return false;
    }
    if (meta.formatVersion != kFormatVersion) {
        if (err) *err = "unsupported manifest version: " + meta.formatVersion;
        return false;
    }
    // file_count 与实际条目数不一致时校验失败（作为恢复/增量基准，不允许不一致的清单）
    if (meta.fileCount != 0 && meta.fileCount != entries.size()) {
        if (err) *err = "file_count mismatch: header=" + std::to_string(meta.fileCount) +
                         ", actual=" + std::to_string(entries.size());
        return false;
    }
    rebuildIndex();
    return true;
}

}  // namespace backup
