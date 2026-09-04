// MainWindow.h - Win32 原生 GUI 主窗口
// 职责：任务列表管理、新建/立即备份、恢复、校验、实时日志与进度显示。
// 零第三方依赖，纯 Win32 API。
#pragma once

#include <atomic>
#include <windows.h>
#include <string>
#include <vector>

#include "gui/TaskStore.h"

namespace backup {
namespace gui {

// 控件 ID
enum ControlId : int {
    IDC_TASKLIST = 1001,
    IDC_BTN_NEW = 1002,
    IDC_BTN_BACKUP = 1003,
    IDC_BTN_RESTORE = 1004,
    IDC_BTN_VERIFY = 1005,
    IDC_BTN_DELETE = 1006,
    IDC_BTN_CANCEL = 1007,
    IDC_LOG = 1008,
    IDC_PROGRESS = 1009,
    IDC_STATUS = 1010,
    IDC_TITLE = 1101,
    IDC_SUBTITLE = 1102,
    IDC_LIST_LABEL = 1103,
    IDC_LOG_LABEL = 1104,
};

// 自定义消息（后台线程 -> UI 线程）
enum CustomMessage : UINT {
    WM_LOG_MSG = WM_USER + 1,      // wParam=0, lParam=wchar_t* 日志文本（调用方分配，UI 释放）
    WM_BACKUP_DONE = WM_USER + 2,  // wParam=success(1/0)
    WM_RESTORE_DONE = WM_USER + 3,
    WM_VERIFY_DONE = WM_USER + 4,
};

class MainWindow {
public:
    MainWindow();
    ~MainWindow();

    // 创建并显示主窗口。返回窗口句柄。
    HWND create(HINSTANCE hInstance);

    // 窗口过程（静态，转发给实例）。
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    HWND hwnd_ = nullptr;
    HINSTANCE hInstance_ = nullptr;

    // 控件句柄
    HWND taskList_ = nullptr;
    HWND logBox_ = nullptr;
    HWND progressBar_ = nullptr;
    HWND statusLabel_ = nullptr;
    HWND titleLabel_ = nullptr;
    HWND subtitleLabel_ = nullptr;
    HWND listLabel_ = nullptr;
    HWND logLabel_ = nullptr;

    // 字体与画刷（create 中创建，析构释放）
    HFONT titleFont_ = nullptr;
    HFONT subtitleFont_ = nullptr;
    HFONT normalFont_ = nullptr;
    HFONT fixedFont_ = nullptr;
    HBRUSH headerBrush_ = nullptr;
    HBRUSH panelBrush_ = nullptr;
    HBRUSH borderBrush_ = nullptr;

    // 任务数据
    std::vector<BackupTask> tasks_;
    int selectedIndex_ = -1;

    // 后台执行状态
    HANDLE workerThread_ = nullptr;
    std::atomic<bool> cancelFlag_{false};
    bool busy_ = false;

    // ---- 窗口创建与布局 ----
    void createControls();
    void layoutControls(int width, int height);

    // ---- 任务管理 ----
    void refreshTaskList();
    void onTaskSelect();
    void onNewTask();
    void onDeleteTask();

    // ---- 操作执行 ----
    void onBackupNow();
    void onRestore();
    void onVerify();
    void onCancel();

    // ---- 后台线程 ----
    struct WorkerParam {
        MainWindow* self = nullptr;
        BackupTask task;
        std::wstring restorePath;  // 恢复时使用
        enum class Op { Backup, Restore, Verify } op = Op::Backup;
    };

    // 启动 worker 线程：关闭旧句柄、创建新线程、失败时回滚 busy 状态。
    void startWorker(WorkerParam::Op op, const BackupTask& task, const std::wstring& restorePath = L"");

    static DWORD WINAPI workerThreadProc(LPVOID param);
    void runBackup(const BackupTask& task);
    void runRestore(const BackupTask& task, const std::wstring& restorePath);
    void runVerify(const BackupTask& task);

    // ---- 日志与状态 ----
    void appendLog(const std::wstring& text);
    void setStatus(const std::wstring& text);
    void setBusy(bool busy);

    // 从后台线程安全地追加日志（分配字符串，PostMessage 给 UI）。
    void postLog(const std::wstring& text);

    // 新建备份对话框（模态）。返回 true 表示用户确认，task 输出新任务。
    bool showNewTaskDialog(BackupTask& outTask);

    // 简单输入对话框（用于恢复目标路径等）。
    bool showInputDialog(const std::wstring& title, const std::wstring& prompt,
                         const std::wstring& defaultValue, std::wstring& outValue);
};

}  // namespace gui
}  // namespace backup
