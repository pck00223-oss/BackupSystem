// MainWindow.h - Qt 主窗口
// 职责：任务表格管理、新建/编辑/删除、立即备份/恢复/校验/取消，
//       进度与状态显示、分级彩色日志。底层复用 backup_core 与 TaskStore。
#pragma once

#include <QMainWindow>

#include <vector>

#include "gui/TaskStore.h"

class QTableWidget;
class QPlainTextEdit;
class QProgressBar;
class QLabel;
class QPushButton;
class Worker;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void appendLog(const QString& text, int level);
    void onTaskFinished(bool ok, const QString& summary);

private:
    // 数据
    std::vector<backup::gui::BackupTask> tasks_;
    Worker* worker_ = nullptr;
    bool busy_ = false;

    // 控件
    QTableWidget* table_ = nullptr;
    QPlainTextEdit* logView_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QLabel* statusLabel_ = nullptr;

    QPushButton* newBtn_ = nullptr;
    QPushButton* editBtn_ = nullptr;
    QPushButton* delBtn_ = nullptr;
    QPushButton* backupBtn_ = nullptr;
    QPushButton* restoreBtn_ = nullptr;
    QPushButton* verifyBtn_ = nullptr;
    QPushButton* cancelBtn_ = nullptr;

    void buildUi();
    void applyStyle();
    void refreshTable();
    int selectedRow() const;
    void setBusy(bool busy);
    void persistTasks();

    void newTask();
    void editTask();
    void deleteTask();
    void backupNow();
    void restoreTask();
    void verifyTask();
    void cancelTask();
};
