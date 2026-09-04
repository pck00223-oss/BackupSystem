// main.cpp - 命令行入口（应用层演示）
// 提供 backup / restore / history 三个子命令，以及基于配置文件的定时备份。
// GUI 在 Phase 9 接入，本入口用于直接验证核心链路。
#include <windows.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "app/BackupTask.h"
#include "app/ConfigLoader.h"
#include "app/TaskScheduler.h"
#include "business/BackupManager.h"
#include "business/RestoreManager.h"
#include "core/Logger.h"
#include "core/TimeUtil.h"
#include "core/Utf.h"
#include "engine/FileSystem.h"

using namespace backup;

namespace {

std::wstring executableDir() {
    wchar_t buf[MAX_PATH];
    const DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring path(buf, n);
    const size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? L"." : path.substr(0, pos);
}

bool hasArg(const std::vector<std::wstring>& args, const std::wstring& key) {
    return std::any_of(args.begin(), args.end(),
                       [&key](const std::wstring& a) { return a == key; });
}

std::wstring getArg(const std::vector<std::wstring>& args, const std::wstring& key,
                    const std::wstring& def = L"") {
    for (size_t i = 0; i + 1 < args.size(); ++i)
        if (args[i] == key) return args[i + 1];
    return def;
}

std::vector<std::wstring> splitExtList(const std::wstring& v) {
    std::vector<std::wstring> out;
    std::wstring cur;
    for (wchar_t ch : v) {
        if (ch == L',') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(ch);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

void printHelp() {
    std::cout <<
        "BackupSystem - 基于 C++/Windows 的数据备份工具\n"
        "用法:\n"
        "  backupapp backup  --source <dir> --target <dir> [选项]\n"
        "      --type full|incremental   备份模式（默认全量）\n"
        "      --include-ext .cpp,.h     仅备份这些扩展名\n"
        "      --exclude-ext .tmp,.log   排除这些扩展名\n"
        "      --schedule HH:MM          设置每天定时备份\n"
        "      --config <file>           配置文件（默认 config/default.conf）\n"
        "  backupapp restore --backup <dir> --to <dir> [--overwrite]\n"
        "  backupapp history --target <dir>\n"
        "  backupapp help\n";
}

void printBackupResult(const BackupResult& res) {
    std::cout << "========== 备份结果 ==========\n";
    std::cout << "任务 ID   : " << wideToUtf8(res.backupId) << "\n";
    std::cout << "模式     : " << (res.mode == BackupMode::Incremental ? "增量备份" : "全量备份") << "\n";
    std::cout << "开始时间 : " << wideToUtf8(res.startTime) << "\n";
    std::cout << "结束时间 : " << wideToUtf8(res.endTime) << "\n";
    std::cout << "源路径   : " << wideToUtf8(res.sourcePath) << "\n";
    std::cout << "目标路径 : " << wideToUtf8(res.targetPath) << "\n";
    std::cout << "扫描条目 : " << res.totalScanned << "\n";
    std::cout << "新增     : " << res.added << "\n";
    std::cout << "修改     : " << res.modified << "\n";
    std::cout << "删除     : " << res.deleted << "\n";
    std::cout << "未变化   : " << res.unchanged << "\n";
    std::cout << "本次写入 : " << res.backedUp << " 个文件, " << res.totalBytes << " 字节\n";
    std::cout << "失败     : " << res.failed << "\n";
    std::cout << "状态     : " << (res.cancelled ? "已取消" : (res.success ? "成功" : "失败")) << "\n";
    for (const auto& e : res.errors) std::cout << "  [错误] " << wideToUtf8(e) << "\n";
    std::cout << "=============================\n";
}

void printRestoreResult(const RestoreResult& res) {
    std::cout << "========== 恢复结果 ==========\n";
    std::cout << "备份目录 : " << wideToUtf8(res.backupRoot) << "\n";
    std::cout << "恢复目录 : " << wideToUtf8(res.restorePath) << "\n";
    std::cout << "开始时间 : " << wideToUtf8(res.startTime) << "\n";
    std::cout << "结束时间 : " << wideToUtf8(res.endTime) << "\n";
    std::cout << "恢复文件 : " << res.restored << "\n";
    std::cout << "跳过     : " << res.skipped << "\n";
    std::cout << "失败     : " << res.failed << "\n";
    std::cout << "Hash校验 : " << res.verified << " 通过, " << res.hashMismatch << " 不一致\n";
    std::cout << "状态     : " << (res.cancelled ? "已取消" : (res.success ? "成功" : "失败")) << "\n";
    for (const auto& w : res.warnings) std::cout << "  [警告] " << wideToUtf8(w) << "\n";
    for (const auto& e : res.errors) std::cout << "  [错误] " << wideToUtf8(e) << "\n";
    std::cout << "=============================\n";
}

int cmdBackup(const std::vector<std::wstring>& args) {
    BackupConfig config;
    const std::wstring configFile =
        getArg(args, L"--config", executableDir() + L"\\config\\default.conf");
    std::vector<std::wstring> warnings;
    ConfigLoader::loadFromFile(configFile, config, warnings);

    const std::wstring src = getArg(args, L"--source");
    const std::wstring tgt = getArg(args, L"--target");
    if (!src.empty()) config.sourcePath = src;
    if (!tgt.empty()) config.targetPath = tgt;

    const std::wstring type = getArg(args, L"--type");
    if (type == L"incremental" || type == L"inc") config.mode = BackupMode::Incremental;
    else if (type == L"full") config.mode = BackupMode::Full;

    const std::wstring inc = getArg(args, L"--include-ext");
    if (!inc.empty()) config.filter.includeExtensions = splitExtList(inc);
    const std::wstring exc = getArg(args, L"--exclude-ext");
    if (!exc.empty()) config.filter.excludeExtensions = splitExtList(exc);

    for (const auto& w : warnings) std::cout << "  [配置] " << wideToUtf8(w) << "\n";

    if (config.sourcePath.empty() || config.targetPath.empty()) {
        std::cout << "错误：需要 --source 与 --target\n";
        printHelp();
        return 1;
    }

    // 初始化日志（写入目标目录下的 logs）
    FileSystem::createDirectories(config.targetPath + L"\\logs");
    Logger::instance().init(config.targetPath + L"\\logs\\backup.log");

    // 定时时间：命令行 --schedule 优先，否则用配置文件里的 schedule
    const std::wstring schedule = getArg(args, L"--schedule");
    if (!schedule.empty()) config.scheduleTime = schedule;

    // 立即执行一次
    BackupTask task(config);
    const BackupResult res = task.run();
    printBackupResult(res);

    // 如果配置了定时时间（来自配置文件或 --schedule），进入定时守护模式
    if (!config.scheduleTime.empty()) {
        TaskScheduler scheduler;
        scheduler.setCallback([](const std::wstring& name, const BackupResult& r) {
            std::cout << "\n[定时任务 " << wideToUtf8(name) << " 完成]\n";
            printBackupResult(r);
        });
        ScheduledTask stask;
        stask.name = L"定时备份";
        stask.config = config;
        stask.scheduleTime = wideToUtf8(config.scheduleTime);
        scheduler.addTask(stask);
        scheduler.start();
        std::cout << "已设置定时备份：每天 " << wideToUtf8(config.scheduleTime)
                  << " 执行（错过会补跑，Ctrl+C 退出）\n";
        while (scheduler.isRunning()) ::Sleep(1000);
        scheduler.stop();
    }
    return (res.success && !res.cancelled) ? 0 : 1;
}

int cmdRestore(const std::vector<std::wstring>& args) {
    const std::wstring backupRoot = getArg(args, L"--backup");
    const std::wstring restoreTo = getArg(args, L"--to");
    if (backupRoot.empty() || restoreTo.empty()) {
        std::cout << "错误：restore 需要 --backup 与 --to\n";
        printHelp();
        return 1;
    }
    RestoreConfig config;
    config.backupRoot = backupRoot;
    config.restorePath = restoreTo;
    config.overwrite = hasArg(args, L"--overwrite");

    FileSystem::createDirectories(backupRoot + L"\\logs");
    Logger::instance().init(backupRoot + L"\\logs\\backup.log");

    const RestoreResult res = RestoreManager::run(config);
    printRestoreResult(res);
    return (res.success && !res.cancelled) ? 0 : 1;
}

int cmdHistory(const std::vector<std::wstring>& args) {
    const std::wstring target = getArg(args, L"--target");
    if (target.empty()) {
        std::cout << "错误：history 需要 --target\n";
        return 1;
    }
    std::ifstream ifs((target + L"\\history.log").c_str(), std::ios::binary);
    if (!ifs) {
        std::cout << "没有历史记录: " << wideToUtf8(target) << "\n";
        return 0;
    }
    std::string line;
    while (std::getline(ifs, line)) std::cout << line << "\n";
    return 0;
}

}  // namespace

int main() {
    // 控制台输出 UTF-8，保证中文与中文路径正常显示
    ::SetConsoleOutputCP(CP_UTF8);

    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::vector<std::wstring> args;
    if (argv) {
        for (int i = 1; i < argc; ++i) args.push_back(argv[i]);
        ::LocalFree(argv);
    }

    if (args.empty() || args[0] == L"help" || args[0] == L"--help" || args[0] == L"-h") {
        printHelp();
        return args.empty() ? 1 : 0;
    }

    if (args[0] == L"backup") return cmdBackup(args);
    if (args[0] == L"restore") return cmdRestore(args);
    if (args[0] == L"history") return cmdHistory(args);

    std::cout << "未知命令: " << wideToUtf8(args[0]) << "\n";
    printHelp();
    return 1;
}
