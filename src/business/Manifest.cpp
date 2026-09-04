// Manifest.cpp - 备份清单实现
#include "business/Manifest.h"

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
    std::ofstream ofs(path.c_str(), std::ios::binary | std::ios::trunc);
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
    for (const auto& e : entries) {
        body += "\n[file]\n";
        body += "path=" + wideToUtf8(e.info.relativePath) + "\n";
        body += "type=" + std::to_string(static_cast<int>(e.info.type)) + "\n";
        body += "size=" + std::to_string(e.info.size) + "\n";
        body += "mtime=" + std::to_string(e.info.modifiedTime) + "\n";
        body += "hash=" + e.info.hash + "\n";
        body += "data=" + wideToUtf8(e.dataPath) + "\n";
    }
    ofs.write(body.data(), static_cast<std::streamsize>(body.size()));
    ofs.flush();
    if (!ofs) {
        if (err) *err = "write manifest failed";
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
            else if (key == "file_count") meta.fileCount = std::stoull(val);
        } else {
            if (key == "path") cur.info.relativePath = utf8ToWide(val);
            else if (key == "type") cur.info.type = static_cast<FileType>(std::stoi(val));
            else if (key == "size") cur.info.size = std::stoull(val);
            else if (key == "mtime") cur.info.modifiedTime = std::stoull(val);
            else if (key == "hash") { cur.info.hash = val; cur.info.hashed = !val.empty(); }
            else if (key == "data") cur.dataPath = utf8ToWide(val);
        }
    }
    flushEntry();

    if (meta.formatVersion != kFormatVersion) {
        if (err) *err = "unsupported manifest version: " + meta.formatVersion;
        return false;
    }
    rebuildIndex();
    return true;
}

}  // namespace backup
