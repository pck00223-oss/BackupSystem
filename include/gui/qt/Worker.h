// Worker.h - Qt 后台执行线程
// 职责：在子线程调用 backup_core 完成备份/恢复/校验，通过信号把日志与结果回传 UI。
// 复用核心业务逻辑，本类只做线程封装与字符串/信号转换。
#pragma once

#include <QString>
#include <QThread>

#include <atomic>
#include <string>

#include "gui/TaskStore.h"

class Worker : public QThread {
    Q_OBJECT
public:
    enum class Op { Backup, Restore, Verify };

    // 日志级别（决定日志面板颜色）。
    enum Level { Info = 0, Success, Warn, Error };

    explicit Worker(QObject* parent = nullptr);

    void configureBackup(const backup::gui::BackupTask& task);
    void configureRestore(const backup::gui::BackupTask& task,
                          const std::wstring& restorePath, bool overwrite);
    void configureVerify(const backup::gui::BackupTask& task);

    void requestCancel();

signals:
    void logMessage(const QString& text, int level);
    void taskFinished(bool success, const QString& summary);

protected:
    void run() override;

private:
    Op op_ = Op::Backup;
    backup::gui::BackupTask task_;
    std::wstring restorePath_;
    bool overwrite_ = true;
    std::atomic<bool> cancel_{false};

    void runBackup();
    void runRestore();
    void runVerify();
    void emitLog(const std::wstring& text, int level = Info);
};
