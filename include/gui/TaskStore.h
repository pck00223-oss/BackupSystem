// TaskStore.h - GUI 任务配置持久化
// 职责：保存/加载用户创建的备份任务列表（名称、源、目标、模式、加密等）。
// 存储格式：UTF-8 文本，制表符分隔，密码用 Windows DPAPI 加密后十六进制编码存储。
// 文件位置：%APPDATA%\BackupSystem\tasks.dat
#pragma once

#include <string>
#include <vector>

#include "core/BackupConfig.h"

namespace backup {
namespace gui {

// 一个备份任务的完整配置（GUI 层使用，包含显示名称）。
struct BackupTask {
    std::wstring name;           // 任务显示名称
    std::wstring sourcePath;     // 源目录
    std::wstring targetPath;     // 备份根目录
    BackupMode mode = BackupMode::Full;
    std::string encryption;      // "none" / "aes256"
    std::string password;        // 加密密码（encryption != "none" 时需要，内存中为明文 UTF-8）
    int keepSnapshots = 0;       // 保留快照数

    // 转换为核心层 BackupConfig。
    BackupConfig toConfig() const;
};

// 任务列表的持久化管理。
class TaskStore {
public:
    // 加载所有任务（文件不存在则返回空列表）。
    static std::vector<BackupTask> load();

    // 保存所有任务（覆盖写入）。
    static bool save(const std::vector<BackupTask>& tasks);

    // 从指定文件加载（测试用，可避免污染真实任务文件）。
    static std::vector<BackupTask> loadFrom(const std::wstring& path);
    // 保存到指定文件（测试用）。
    static bool saveTo(const std::wstring& path, const std::vector<BackupTask>& tasks);

    // 获取存储文件路径。
    static std::wstring storePath();

private:
    // DPAPI 加密密码 → 十六进制字符串（仅当前用户可解密）。
    static std::string encryptPassword(const std::string& passwordUtf8);
    // 十六进制字符串 → DPAPI 解密密码（UTF-8）。
    static std::string decryptPassword(const std::string& hexEncrypted);
};

}  // namespace gui
}  // namespace backup
