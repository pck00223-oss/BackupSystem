// ConfigLoader.cpp - 配置文件解析实现
#include "app/ConfigLoader.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "core/Utf.h"
#include "engine/FileSystem.h"  // nowSeconds：用于"最近 N 天修改"时间阈值计算

namespace backup {

namespace {

std::string trimA(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t')) --e;
    return s.substr(b, e - b);
}

// 拆分逗号分隔的扩展名列表，统一为小写。
std::vector<std::wstring> splitExtList(const std::string& v) {
    std::vector<std::wstring> out;
    std::istringstream ss(v);
    std::string item;
    while (std::getline(ss, item, ',')) {
        item = trimA(item);
        if (item.empty()) continue;
        std::wstring w = utf8ToWide(item);
        std::transform(w.begin(), w.end(), w.begin(),
                       [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
        if (w[0] != L'.') w = L"." + w;  // 统一为 .cpp 形式
        out.push_back(w);
    }
    return out;
}

// 解析大小，支持 KB/MB 后缀。
uint64_t parseSize(const std::string& v) {
    std::string s = trimA(v);
    uint64_t mult = 1;
    if (!s.empty()) {
        const char last = s.back();
        if (last == 'K' || last == 'k') { mult = 1024; s.pop_back(); }
        else if (last == 'M' || last == 'm') { mult = 1024 * 1024; s.pop_back(); }
        else if (last == 'G' || last == 'g') { mult = 1024 * 1024 * 1024; s.pop_back(); }
    }
    try {
        return std::stoull(trimA(s)) * mult;
    } catch (...) {
        return 0;
    }
}

}  // namespace

bool ConfigLoader::loadFromFile(const std::wstring& path,
                                BackupConfig& out,
                                std::vector<std::wstring>& warnings) {
    std::ifstream ifs(path.c_str(), std::ios::binary);
    if (!ifs) return false;
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(ifs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
    }
    return parseLines(lines, out, warnings);
}

bool ConfigLoader::parseLines(const std::vector<std::string>& lines,
                              BackupConfig& out,
                              std::vector<std::wstring>& warnings) {
    for (const auto& raw : lines) {
        const std::string line = trimA(raw);
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            warnings.push_back(L"忽略无法解析的行: " + utf8ToWide(line));
            continue;
        }
        const std::string key = trimA(line.substr(0, eq));
        const std::string val = trimA(line.substr(eq + 1));

        if (key == "source") {
            out.sourcePath = utf8ToWide(val);
        } else if (key == "target") {
            out.targetPath = utf8ToWide(val);
        } else if (key == "type") {
            if (val == "incremental" || val == "inc") out.mode = BackupMode::Incremental;
            else if (val == "full") out.mode = BackupMode::Full;
            else warnings.push_back(L"未知备份类型: " + utf8ToWide(val) + L"，使用全量");
        } else if (key == "schedule") {
            out.scheduleTime = utf8ToWide(val);
        } else if (key == "include_ext") {
            out.filter.includeExtensions = splitExtList(val);
        } else if (key == "exclude_ext") {
            out.filter.excludeExtensions = splitExtList(val);
        } else if (key == "include_path") {
            out.filter.includeSubPaths.push_back(utf8ToWide(val));
        } else if (key == "exclude_path") {
            out.filter.excludeSubPaths.push_back(utf8ToWide(val));
        } else if (key == "min_size") {
            out.filter.minSize = parseSize(val);
        } else if (key == "max_size") {
            out.filter.maxSize = parseSize(val);
        } else if (key == "modified_within_days") {
            const uint64_t days = parseSize(val);
            if (days > 0) {
                out.filter.modifiedAfterEnabled = true;
                out.filter.modifiedAfterUnix = FileSystem::nowSeconds() - days * 86400ULL;
            }
        } else if (key == "skip_empty") {
            out.filter.skipEmptyFiles = (val == "1" || val == "true" || val == "yes");
        } else if (key == "overwrite_restore") {
            out.overwriteOnRestore = (val == "1" || val == "true" || val == "yes");
        } else if (key == "keep_snapshots") {
            try {
                const int n = std::stoi(val);
                if (n >= 0) out.keepSnapshots = n;
                else warnings.push_back(L"keep_snapshots 不能为负数，忽略: " + utf8ToWide(val));
            } catch (...) {
                warnings.push_back(L"keep_snapshots 格式无效，忽略: " + utf8ToWide(val));
            }
        } else {
            warnings.push_back(L"未知配置项: " + utf8ToWide(key));
        }
    }
    return true;
}

}  // namespace backup
