// TaskStore.cpp - GUI 任务配置持久化实现
#include "gui/TaskStore.h"

#include <fstream>
#include <sstream>

#include <windows.h>
#include <shlobj.h>

namespace backup {
namespace gui {

BackupConfig BackupTask::toConfig() const {
    BackupConfig cfg;
    cfg.sourcePath = sourcePath;
    cfg.targetPath = targetPath;
    cfg.mode = mode;
    cfg.encryption = encryption;
    cfg.password = password;
    cfg.keepSnapshots = keepSnapshots;
    return cfg;
}

std::wstring TaskStore::storePath() {
    wchar_t appData[MAX_PATH] = {0};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
        std::wstring dir = std::wstring(appData) + L"\\BackupSystem";
        CreateDirectoryW(dir.c_str(), nullptr);
        return dir + L"\\tasks.dat";
    }
    return L"tasks.dat";  // fallback: 当前目录
}

std::vector<BackupTask> TaskStore::load() {
    std::vector<BackupTask> tasks;
    std::wifstream file(storePath().c_str());
    if (!file.is_open()) return tasks;

    std::wstring line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        BackupTask task;
        std::wstringstream ss(line);
        std::wstring token;

        // 字段顺序：name \t source \t target \t mode \t encryption \t password \t keepSnapshots
        if (!std::getline(ss, token, L'\t')) continue;
        task.name = token;
        if (!std::getline(ss, token, L'\t')) continue;
        task.sourcePath = token;
        if (!std::getline(ss, token, L'\t')) continue;
        task.targetPath = token;
        if (!std::getline(ss, token, L'\t')) continue;
        task.mode = (token == L"incremental") ? BackupMode::Incremental : BackupMode::Full;
        if (!std::getline(ss, token, L'\t')) continue;
        task.encryption = std::string(token.begin(), token.end());
        if (!std::getline(ss, token, L'\t')) continue;
        task.password = std::string(token.begin(), token.end());
        if (std::getline(ss, token, L'\t')) {
            try { task.keepSnapshots = std::stoi(token); } catch (...) { task.keepSnapshots = 0; }
        }

        tasks.push_back(task);
    }
    return tasks;
}

bool TaskStore::save(const std::vector<BackupTask>& tasks) {
    std::wofstream file(storePath().c_str(), std::ios::trunc);
    if (!file.is_open()) return false;

    for (const auto& task : tasks) {
        file << task.name << L'\t'
             << task.sourcePath << L'\t'
             << task.targetPath << L'\t'
             << (task.mode == BackupMode::Incremental ? L"incremental" : L"full") << L'\t'
             << std::wstring(task.encryption.begin(), task.encryption.end()) << L'\t'
             << std::wstring(task.password.begin(), task.password.end()) << L'\t'
             << task.keepSnapshots << L'\n';
    }
    return true;
}

}  // namespace gui
}  // namespace backup
