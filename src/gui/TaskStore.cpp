// TaskStore.cpp - GUI 任务配置持久化实现（UTF-8 + DPAPI 加密密码）
#include "gui/TaskStore.h"

#include <fstream>
#include <sstream>
#include <vector>

#include <windows.h>
#include <dpapi.h>
#include <shlobj.h>

#include "core/Utf.h"

#pragma comment(lib, "crypt32.lib")

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
    return L"tasks.dat";
}

std::string TaskStore::encryptPassword(const std::string& passwordUtf8) {
    if (passwordUtf8.empty()) return "";
    DATA_BLOB in, out;
    in.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(passwordUtf8.data()));
    in.cbData = (DWORD)passwordUtf8.size();
    if (!CryptProtectData(&in, L"BackupSystem_Password", nullptr, nullptr, nullptr, 0, &out)) {
        return "";
    }
    std::string hex;
    hex.reserve(out.cbData * 2);
    for (DWORD i = 0; i < out.cbData; ++i) {
        char buf[3];
        sprintf_s(buf, "%02x", out.pbData[i]);
        hex += buf;
    }
    LocalFree(out.pbData);
    return hex;
}

std::string TaskStore::decryptPassword(const std::string& hexEncrypted) {
    if (hexEncrypted.empty() || hexEncrypted.size() % 2 != 0) return "";
    std::vector<BYTE> data(hexEncrypted.size() / 2);
    for (size_t i = 0; i < data.size(); ++i) {
        unsigned int byte = 0;
        if (sscanf_s(hexEncrypted.c_str() + i * 2, "%02x", &byte) != 1) return "";
        data[i] = (BYTE)byte;
    }
    DATA_BLOB in, out;
    in.pbData = data.data();
    in.cbData = (DWORD)data.size();
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out)) {
        return "";
    }
    std::string password(reinterpret_cast<const char*>(out.pbData), out.cbData);
    LocalFree(out.pbData);
    return password;
}

std::vector<BackupTask> TaskStore::loadFrom(const std::wstring& path) {
    std::vector<BackupTask> tasks;
    std::ifstream file(wideToUtf8(path));
    if (!file.is_open()) return tasks;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        BackupTask task;
        std::stringstream ss(line);
        std::string token;

        // 字段顺序（UTF-8，制表符分隔）：
        // name \t source \t target \t mode \t encryption \t encryptedPassword \t keepSnapshots
        if (!std::getline(ss, token, '\t')) continue;
        task.name = utf8ToWide(token);
        if (!std::getline(ss, token, '\t')) continue;
        task.sourcePath = utf8ToWide(token);
        if (!std::getline(ss, token, '\t')) continue;
        task.targetPath = utf8ToWide(token);
        if (!std::getline(ss, token, '\t')) continue;
        task.mode = (token == "incremental") ? BackupMode::Incremental : BackupMode::Full;
        if (!std::getline(ss, token, '\t')) continue;
        task.encryption = token;
        if (!std::getline(ss, token, '\t')) continue;
        task.password = decryptPassword(token);  // DPAPI 解密
        if (std::getline(ss, token, '\t')) {
            try { task.keepSnapshots = std::stoi(token); } catch (...) { task.keepSnapshots = 0; }
        }

        tasks.push_back(task);
    }
    return tasks;
}

std::vector<BackupTask> TaskStore::load() {
    return loadFrom(storePath());
}

bool TaskStore::saveTo(const std::wstring& path, const std::vector<BackupTask>& tasks) {
    std::ofstream file(wideToUtf8(path), std::ios::trunc);
    if (!file.is_open()) return false;

    for (const auto& task : tasks) {
        const std::string encPwd = encryptPassword(task.password);  // DPAPI 加密
        file << wideToUtf8(task.name) << '\t'
             << wideToUtf8(task.sourcePath) << '\t'
             << wideToUtf8(task.targetPath) << '\t'
             << (task.mode == BackupMode::Incremental ? "incremental" : "full") << '\t'
             << task.encryption << '\t'
             << (encPwd.empty() ? "" : encPwd) << '\t'
             << task.keepSnapshots << '\n';
    }
    return true;
}

bool TaskStore::save(const std::vector<BackupTask>& tasks) {
    return saveTo(storePath(), tasks);
}

}  // namespace gui
}  // namespace backup
