// main.cpp - 命令行入口（应用层演示）
// 提供 backup / restore / verify / history / schedule 子命令。
// schedule 子命令将备份注册为 Windows 计划任务，到点自动执行后退出，无需程序常驻。
#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "app/BackupTask.h"
#include "app/ConfigLoader.h"
#include "app/ScheduleManager.h"
#include "app/TaskScheduler.h"
#include "business/BackupManager.h"
#include "business/RestoreManager.h"
#include "business/VerifyManager.h"
#include "core/Logger.h"
#include "core/TimeUtil.h"
#include "core/Utf.h"
#include "engine/FileSystem.h"

using namespace backup;

namespace {

std::wstring executablePath() {
    wchar_t buf[MAX_PATH];
    const DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::wstring(buf, n);
}

std::wstring executableDir() {
    std::wstring path = executablePath();
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
            if (!cur.empty()) {
                std::transform(cur.begin(), cur.end(), cur.begin(),
                               [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
                if (cur[0] != L'.') cur = L"." + cur;
                out.push_back(cur);
            }
            cur.clear();
        } else {
            cur.push_back(ch);
        }
    }
    if (!cur.empty()) {
        std::transform(cur.begin(), cur.end(), cur.begin(),
                       [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        if (cur[0] != L'.') cur = L"." + cur;
        out.push_back(cur);
    }
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
        "      --schedule HH:MM          程序常驻定时备份（不推荐长期自用）\n"
        "      --config <file>           配置文件（默认 config/default.conf）\n"
        "  backupapp restore --backup <dir> --to <dir> [--overwrite]\n"
        "  backupapp verify  --backup <dir> [--repair --source <dir>]\n"
        "      --repair --source <dir>  发现损坏/缺失时用源文件自动重建\n"
        "  backupapp history --target <dir>\n"
        "  backupapp schedule --register --time HH:MM --source <dir> --target <dir> [--type full|inc] [--name <task>]\n"
        "      注册为 Windows 计划任务，到点自动备份后退出（推荐长期自用）\n"
        "  backupapp schedule --unregister [--name <task>]\n"
        "  backupapp schedule --status [--name <task>]\n"
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
    for (const auto& w : res.warnings) std::cout << "  [警告] " << wideToUtf8(w) << "\n";
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

    FileSystem::createDirectories(config.targetPath + L"\\logs");
    Logger::instance().init(config.targetPath + L"\\logs\\backup.log");

    const std::wstring schedule = getArg(args, L"--schedule");
    if (!schedule.empty()) config.scheduleTime = schedule;

    // 常驻定时模式（--schedule）：程序一直开着才会触发。
    // 长期自用推荐用 schedule --register 注册 Windows 计划任务。
    if (!config.scheduleTime.empty()) {
        // 校验时间格式 HH:MM（00:00-23:59），非法则报错退出
        int schedHour = 0, schedMinute = 0;
        if (!parseHHMM(config.scheduleTime, schedHour, schedMinute)) {
            std::cout << "错误：--schedule 时间格式应为 HH:MM（00:00-23:59），当前: "
                      << wideToUtf8(config.scheduleTime) << "\n";
            return 1;
        }

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
        std::cout << "已设置常驻定时备份：每天 " << wideToUtf8(config.scheduleTime)
                  << " 执行（错过会补跑，Ctrl+C 退出）\n"
                  << "提示：长期自用推荐用 'backupapp schedule --register' 注册 Windows 计划任务，无需程序常驻。\n";
        while (scheduler.isRunning()) ::Sleep(1000);
        scheduler.stop();
        return 0;
    }

    BackupTask task(config);
    const BackupResult res = task.run();
    printBackupResult(res);
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

int cmdVerify(const std::vector<std::wstring>& args) {
    const std::wstring backupRoot = getArg(args, L"--backup");
    if (backupRoot.empty()) {
        std::cout << "错误：verify 需要 --backup <备份目录>\n";
        return 1;
    }

    std::cout << "========== 完整性校验 ==========\n";
    std::cout << "备份目录 : " << wideToUtf8(backupRoot) << "\n";

    VerifyOptions vopts;
    vopts.progress = [](const std::wstring& rel) {
        std::cout << "  校验中: " << wideToUtf8(rel) << "\r";
    };
    // --repair：发现损坏/缺失时，用源目录文件自动重建
    vopts.repair = hasArg(args, L"--repair");
    vopts.sourcePath = getArg(args, L"--source");
    if (vopts.repair && vopts.sourcePath.empty()) {
        std::cout << "错误：--repair 需要同时指定 --source <源目录>\n";
        return 1;
    }
    if (vopts.repair) {
        std::cout << "修复模式 : 开启 (源目录: " << wideToUtf8(vopts.sourcePath) << ")\n";
    }
    const VerifyResult res = VerifyManager::run(backupRoot, vopts);
    std::cout << "                                        \r";

    std::cout << "文件总数 : " << res.total << "\n";
    std::cout << "通过     : " << res.passed << "\n";
    std::cout << "缺失     : " << res.missing << "\n";
    std::cout << "损坏     : " << res.corrupted << "\n";
    if (res.repaired > 0) std::cout << "已修复   : " << res.repaired << "\n";
    std::cout << "跳过     : " << res.skipped << " (目录/符号链接)\n";
    std::cout << "残留     : " << res.residual << " (.baktmp/.baktmp.old)\n";
    std::cout << "状态     : " << (res.success ? "完整" : "不完整") << "\n";
    for (const auto& e : res.errors) std::cout << "  " << wideToUtf8(e) << "\n";
    std::cout << "================================\n";
    return res.success ? 0 : 1;
}

int cmdSchedule(const std::vector<std::wstring>& args) {
    const std::wstring taskName = getArg(args, L"--name", ScheduleManager::defaultTaskName());

    // schedule --status
    if (hasArg(args, L"--status")) {
        std::cout << "任务名   : " << wideToUtf8(taskName) << "\n";
        if (ScheduleManager::isRegistered(taskName)) {
            std::cout << "状态     : 已注册\n";
            const std::wstring next = ScheduleManager::getNextRunTime(taskName);
            if (!next.empty()) std::cout << "下次运行 : " << wideToUtf8(next) << "\n";
        } else {
            std::cout << "状态     : 未注册\n";
        }
        return 0;
    }

    // schedule --unregister
    if (hasArg(args, L"--unregister")) {
        std::string err;
        if (ScheduleManager::unregister(taskName, &err)) {
            std::cout << "已卸载任务: " << wideToUtf8(taskName) << "\n";
            return 0;
        }
        std::cout << "卸载失败: " << err << "\n";
        return 1;
    }

    // schedule --register
    if (hasArg(args, L"--register")) {
        const std::wstring time = getArg(args, L"--time");
        const std::wstring src = getArg(args, L"--source");
        const std::wstring tgt = getArg(args, L"--target");
        if (time.empty() || src.empty() || tgt.empty()) {
            std::cout << "错误：schedule --register 需要 --time HH:MM --source <dir> --target <dir>\n";
            printHelp();
            return 1;
        }

        // 校验时间格式 HH:MM（00:00-23:59）
        int timeHour = 0, timeMinute = 0;
        if (!parseHHMM(time, timeHour, timeMinute)) {
            std::cout << "错误：--time 格式应为 HH:MM（00:00-23:59），当前: " << wideToUtf8(time) << "\n";
            return 1;
        }

        // 校验任务名：只允许字母、数字、中文、下划线、连字符、空格、点号
        for (wchar_t c : taskName) {
            const bool ok = (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
                            (c >= L'0' && c <= L'9') || c == L'_' || c == L'-' ||
                            c == L' ' || c == L'.' || c > 0x7F;
            if (!ok) {
                std::cout << "错误：任务名包含非法字符: " << wideToUtf8(taskName) << "\n";
                return 1;
            }
        }

        const std::wstring type = getArg(args, L"--type", L"full");
        const std::wstring modeArg = (type == L"inc" || type == L"incremental") ? L"incremental" : L"full";

        // 构造任务执行的命令行参数：backupapp backup --source <src> --target <tgt> --type <mode>
        // 路径含空格或引号时用引号包裹（内部引号转义）
        const auto quote = [](const std::wstring& s) -> std::wstring {
            if (s.find(L' ') == std::wstring::npos && s.find(L'"') == std::wstring::npos) return s;
            std::wstring escaped;
            escaped.reserve(s.size() + 4);
            for (wchar_t c : s) {
                if (c == L'"') escaped += L"\\\"";
                else escaped += c;
            }
            return L"\"" + escaped + L"\"";
        };
        const std::wstring arguments =
            L"backup --source " + quote(src) +
            L" --target " + quote(tgt) +
            L" --type " + modeArg;

        const std::wstring exe = executablePath();
        std::string err;
        if (ScheduleManager::registerDaily(taskName, exe, arguments, time, &err)) {
            std::cout << "已注册 Windows 计划任务:\n";
            std::cout << "  任务名 : " << wideToUtf8(taskName) << "\n";
            std::cout << "  程序   : " << wideToUtf8(exe) << "\n";
            std::cout << "  参数   : " << wideToUtf8(arguments) << "\n";
            std::cout << "  时间   : 每天 " << wideToUtf8(time) << "\n";
            std::cout << "到点将自动执行备份，完成后退出，无需程序常驻。\n";
            std::cout << "注意: 任务使用交互式登录令牌，用户登录/锁屏状态下到点触发；注销后已排队任务可能执行一次，未登录时不会主动启动新任务。\n";
            std::cout << "查看状态: backupapp schedule --status\n";
            std::cout << "卸载任务: backupapp schedule --unregister\n";
            return 0;
        }
        std::cout << "注册失败: " << err << "\n";
        return 1;
    }

    std::cout << "用法：schedule --register | --unregister | --status\n";
    printHelp();
    return 1;
}

}  // namespace

int main() {
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
    if (args[0] == L"verify") return cmdVerify(args);
    if (args[0] == L"history") return cmdHistory(args);
    if (args[0] == L"schedule") return cmdSchedule(args);

    std::cout << "未知命令: " << wideToUtf8(args[0]) << "\n";
    printHelp();
    return 1;
}
