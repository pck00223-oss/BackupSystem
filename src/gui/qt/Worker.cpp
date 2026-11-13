// Worker.cpp - Qt 后台执行线程实现
#include "gui/qt/Worker.h"

#include <string>

#include "business/BackupManager.h"
#include "business/RestoreManager.h"
#include "business/SnapshotManager.h"
#include "business/VerifyManager.h"

Worker::Worker(QObject* parent) : QThread(parent) {}

void Worker::configureBackup(const backup::gui::BackupTask& task) {
    op_ = Op::Backup;
    task_ = task;
}

void Worker::configureRestore(const backup::gui::BackupTask& task,
                              const std::wstring& restorePath, bool overwrite) {
    op_ = Op::Restore;
    task_ = task;
    restorePath_ = restorePath;
    overwrite_ = overwrite;
}

void Worker::configureVerify(const backup::gui::BackupTask& task) {
    op_ = Op::Verify;
    task_ = task;
}

void Worker::requestCancel() {
    cancel_.store(true);
}

void Worker::emitLog(const std::wstring& text, int level) {
    emit logMessage(QString::fromStdWString(text), level);
}

void Worker::run() {
    cancel_.store(false);
    bool ok = false;
    QString summary;
    try {
        if (op_ == Op::Backup) {
            runBackup();
        } else if (op_ == Op::Restore) {
            runRestore();
        } else {
            runVerify();
        }
    } catch (const std::exception& e) {
        emitLog(L"[错误] 后台线程异常: " +
                    QString::fromUtf8(e.what()).toStdWString(),
                Error);
    } catch (...) {
        emitLog(L"[错误] 后台线程发生未知异常", Error);
    }
    // 成功与否由各子流程通过最后一条日志体现；这里用取消标志兜底汇总。
    ok = !cancel_.load();
    summary = cancel_.load() ? QStringLiteral("已取消") : QStringLiteral("执行结束");
    emit taskFinished(ok, summary);
}

void Worker::runBackup() {
    backup::BackupConfig cfg = task_.toConfig();
    backup::BackupManager::Options opts;
    opts.cancelCheck = [this]() { return cancel_.load(); };
    opts.progress = [this](const std::wstring& path) {
        emitLog(L"  处理: " + path);
    };

    emitLog(L"---------- 备份结果 ----------");
    backup::BackupResult res = backup::BackupManager::run(cfg, opts);

    emitLog(L"  状态: " +
                std::wstring(res.success ? L"成功"
                                         : (res.cancelled ? L"已取消" : L"失败")),
            res.success ? Success : (res.cancelled ? Warn : Error));
    emitLog(L"  扫描: " + std::to_wstring(res.totalScanned) + L" 个文件");
    emitLog(L"  写入: " + std::to_wstring(res.backedUp) + L" 个文件, " +
                std::to_wstring(res.totalBytes) + L" 字节");
    emitLog(L"  新增: " + std::to_wstring(res.added) +
                L"  修改: " + std::to_wstring(res.modified) +
                L"  删除: " + std::to_wstring(res.deleted) +
                L"  失败: " + std::to_wstring(res.failed));
    for (const auto& err : res.errors) emitLog(L"  [错误] " + err, Error);
    for (const auto& warn : res.warnings) emitLog(L"  [警告] " + warn, Warn);
    emitLog(L"================================");
}

void Worker::runRestore() {
    backup::RestoreConfig cfg;
    cfg.backupRoot = task_.targetPath;
    cfg.restorePath = restorePath_;
    cfg.overwrite = overwrite_;
    cfg.password = task_.password;

    emitLog(L"---------- 恢复结果 ----------");
    backup::RestoreResult res = backup::RestoreManager::run(
        cfg,
        [this]() { return cancel_.load(); },
        [this](const std::wstring& path) { emitLog(L"  恢复: " + path); });

    emitLog(L"  状态: " +
                std::wstring(res.success ? L"成功"
                                         : (res.cancelled ? L"已取消" : L"失败")),
            res.success ? Success : (res.cancelled ? Warn : Error));
    emitLog(L"  恢复: " + std::to_wstring(res.restored) +
                L"  跳过: " + std::to_wstring(res.skipped) +
                L"  失败: " + std::to_wstring(res.failed));
    emitLog(L"  Hash校验: " + std::to_wstring(res.verified) + L" 通过, " +
                std::to_wstring(res.hashMismatch) + L" 不一致");
    for (const auto& err : res.errors) emitLog(L"  [错误] " + err, Error);
    for (const auto& warn : res.warnings) emitLog(L"  [警告] " + warn, Warn);
    emitLog(L"================================");
}

void Worker::runVerify() {
    backup::VerifyOptions opts;
    opts.cancelCheck = [this]() { return cancel_.load(); };
    opts.progress = [this](const std::wstring& path) {
        emitLog(L"  校验: " + path);
    };

    backup::VerifyResult res = backup::VerifyManager::run(task_.targetPath, opts);
    bool allOk = res.success;

    emitLog(L"---------- 根目录校验 ----------");
    emitLog(L"  总数: " + std::to_wstring(res.total) +
                L"  通过: " + std::to_wstring(res.passed) +
                L"  缺失: " + std::to_wstring(res.missing) +
                L"  损坏: " + std::to_wstring(res.corrupted) +
                L"  残留: " + std::to_wstring(res.residual) +
                L"  状态: " + (res.success ? L"完整" : L"不完整"),
            res.success ? Success : Error);
    for (const auto& err : res.errors) emitLog(L"  [错误] " + err, Error);

    const std::vector<backup::SnapshotInfo> snaps =
        backup::SnapshotManager::listSnapshots(task_.targetPath);
    if (!snaps.empty()) {
        emitLog(L"---------- 快照校验 (" + std::to_wstring(snaps.size()) +
                    L" 份) ----------");
    }
    for (const auto& s : snaps) {
        backup::VerifyResult vr = backup::VerifyManager::run(s.path, opts);
        allOk = allOk && vr.success;
        emitLog(L"  快照 " + s.timestamp + L": 文件 " +
                    std::to_wstring(vr.total) + L" 通过 " +
                    std::to_wstring(vr.passed) + L" 缺失 " +
                    std::to_wstring(vr.missing) + L" 损坏 " +
                    std::to_wstring(vr.corrupted) + L" 残留 " +
                    std::to_wstring(vr.residual) + L" 状态: " +
                    (vr.success ? L"完整" : L"不完整"),
                vr.success ? Info : Error);
        for (const auto& err : vr.errors) emitLog(L"    [错误] " + err, Error);
    }
    emitLog(L"========== 校验总状态: " +
                std::wstring(allOk ? L"完整" : L"不完整") + L" ==========",
            allOk ? Success : Error);
    emitLog(L"================================");
}
