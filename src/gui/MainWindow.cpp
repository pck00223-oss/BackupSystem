// MainWindow.cpp - Win32 原生 GUI 主窗口实现
#include "gui/MainWindow.h"

#include <commctrl.h>
#include <shlobj.h>

#include <algorithm>
#include <sstream>

#include "business/BackupManager.h"
#include "business/RestoreManager.h"
#include "business/VerifyManager.h"
#include "core/Utf.h"

#pragma comment(lib, "comctl32.lib")

namespace backup {
namespace gui {

// ============ 前置声明 ============
static INT_PTR CALLBACK newTaskDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
static INT_PTR CALLBACK inputDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);

// ============ 构造/析构 ============
MainWindow::MainWindow() = default;

MainWindow::~MainWindow() {
    if (workerThread_) {
        cancelFlag_ = true;
        WaitForSingleObject(workerThread_, 5000);
        CloseHandle(workerThread_);
    }
}

// ============ 窗口创建 ============
HWND MainWindow::create(HINSTANCE hInstance) {
    hInstance_ = hInstance;
    tasks_ = TaskStore::load();

    // 注册窗口类
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"BackupSystemMainWindow";
    RegisterClassExW(&wc);

    // 注册对话框窗口类（用 CreateWindowEx 手动创建，无需 .rc 资源）
    WNDCLASSEXW dlgWC = {0};
    dlgWC.cbSize = sizeof(dlgWC);
    dlgWC.style = CS_HREDRAW | CS_VREDRAW;
    dlgWC.lpfnWndProc = (WNDPROC)newTaskDlgProc;
    dlgWC.hInstance = hInstance;
    dlgWC.hCursor = LoadCursor(nullptr, IDC_ARROW);
    dlgWC.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    dlgWC.lpszClassName = L"NewTaskDlg";
    RegisterClassExW(&dlgWC);

    dlgWC.lpfnWndProc = (WNDPROC)inputDlgProc;
    dlgWC.lpszClassName = L"InputDlg";
    RegisterClassExW(&dlgWC);

    // 创建窗口
    hwnd_ = CreateWindowExW(
        0, L"BackupSystemMainWindow", L"BackupSystem - 数据备份工具",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 900, 600,
        nullptr, nullptr, hInstance, this);

    if (!hwnd_) return nullptr;

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    return hwnd_;
}

// ============ 窗口过程 ============
LRESULT CALLBACK MainWindow::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MainWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = (CREATESTRUCTW*)lParam;
        self = (MainWindow*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        self->hwnd_ = hwnd;
    } else {
        self = (MainWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }

    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
        case WM_CREATE:
            self->createControls();
            self->refreshTaskList();
            return 0;

        case WM_SIZE: {
            int w = LOWORD(lParam);
            int h = HIWORD(lParam);
            self->layoutControls(w, h);
            return 0;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == IDC_BTN_NEW) self->onNewTask();
            else if (id == IDC_BTN_BACKUP) self->onBackupNow();
            else if (id == IDC_BTN_RESTORE) self->onRestore();
            else if (id == IDC_BTN_VERIFY) self->onVerify();
            else if (id == IDC_BTN_DELETE) self->onDeleteTask();
            else if (id == IDC_TASKLIST && HIWORD(wParam) == LBN_SELCHANGE) self->onTaskSelect();
            return 0;
        }

        case WM_LOG_MSG: {
            auto* text = (wchar_t*)lParam;
            if (text) {
                self->appendLog(std::wstring(text));
                delete[] text;
            }
            return 0;
        }

        case WM_BACKUP_DONE:
        case WM_RESTORE_DONE:
        case WM_VERIFY_DONE: {
            self->setBusy(false);
            self->setStatus(wParam ? L"完成" : L"失败");
            return 0;
        }

        case WM_CLOSE:
            if (self->busy_) {
                if (MessageBoxW(hwnd, L"有任务正在执行，确定要退出吗？", L"确认退出",
                                MB_YESNO | MB_ICONQUESTION) != IDYES) {
                    return 0;
                }
                self->cancelFlag_ = true;
            }
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ============ 控件创建与布局 ============
void MainWindow::createControls() {
    // 任务列表
    taskList_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
        0, 0, 0, 0, hwnd_, (HMENU)IDC_TASKLIST, hInstance_, nullptr);

    // 按钮
    struct Btn { int id; const wchar_t* text; };
    Btn buttons[] = {
        {IDC_BTN_NEW, L"新建任务"},
        {IDC_BTN_BACKUP, L"立即备份"},
        {IDC_BTN_RESTORE, L"恢复"},
        {IDC_BTN_VERIFY, L"校验"},
        {IDC_BTN_DELETE, L"删除任务"},
    };
    for (const auto& b : buttons) {
        CreateWindowExW(0, L"BUTTON", b.text,
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0, hwnd_, (HMENU)b.id, hInstance_, nullptr);
    }

    // 日志区（只读多行编辑框）
    logBox_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        0, 0, 0, 0, hwnd_, (HMENU)IDC_LOG, hInstance_, nullptr);
    SendMessageW(logBox_, WM_SETFONT, (WPARAM)GetStockObject(ANSI_FIXED_FONT), TRUE);

    // 进度条
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&icc);
    progressBar_ = CreateWindowExW(0, PROGRESS_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
        0, 0, 0, 0, hwnd_, (HMENU)IDC_PROGRESS, hInstance_, nullptr);

    // 状态栏文本
    statusLabel_ = CreateWindowExW(0, L"STATIC", L"就绪",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 0, 0, hwnd_, (HMENU)IDC_STATUS, hInstance_, nullptr);
}

void MainWindow::layoutControls(int width, int height) {
    const int margin = 8;
    const int btnHeight = 30;
    const int btnWidth = 80;
    const int listWidth = 220;
    const int statusHeight = 20;
    const int progressHeight = 18;

    // 左侧任务列表
    MoveWindow(taskList_, margin, margin, listWidth, height - margin * 2, TRUE);

    // 右侧按钮行
    int btnX = margin + listWidth + margin;
    int btnY = margin;
    HWND btns[] = {GetDlgItem(hwnd_, IDC_BTN_NEW), GetDlgItem(hwnd_, IDC_BTN_BACKUP),
                    GetDlgItem(hwnd_, IDC_BTN_RESTORE), GetDlgItem(hwnd_, IDC_BTN_VERIFY),
                    GetDlgItem(hwnd_, IDC_BTN_DELETE)};
    for (HWND btn : btns) {
        MoveWindow(btn, btnX, btnY, btnWidth, btnHeight, TRUE);
        btnX += btnWidth + 4;
    }

    // 日志区（按钮下方）
    int logY = btnY + btnHeight + margin;
    int logHeight = height - logY - progressHeight - statusHeight - margin * 2;
    MoveWindow(logBox_, margin + listWidth + margin, logY,
               width - listWidth - margin * 3, logHeight, TRUE);

    // 进度条
    int progY = logY + logHeight + margin;
    MoveWindow(progressBar_, margin + listWidth + margin, progY,
               width - listWidth - margin * 3, progressHeight, TRUE);

    // 状态栏
    int statY = progY + progressHeight + 2;
    MoveWindow(statusLabel_, margin + listWidth + margin, statY,
               width - listWidth - margin * 3, statusHeight, TRUE);
}

// ============ 任务管理 ============
void MainWindow::refreshTaskList() {
    SendMessageW(taskList_, LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < tasks_.size(); ++i) {
        const auto& t = tasks_[i];
        std::wstring display = t.name + L"  [" +
            (t.mode == BackupMode::Incremental ? L"增量" : L"全量") + L"]";
        if (t.encryption == "aes256") display += L" [加密]";
        SendMessageW(taskList_, LB_ADDSTRING, 0, (LPARAM)display.c_str());
    }
    if (selectedIndex_ >= 0 && selectedIndex_ < (int)tasks_.size()) {
        SendMessageW(taskList_, LB_SETCURSEL, selectedIndex_, 0);
    }
}

void MainWindow::onTaskSelect() {
    selectedIndex_ = (int)SendMessageW(taskList_, LB_GETCURSEL, 0, 0);
    if (selectedIndex_ >= 0 && selectedIndex_ < (int)tasks_.size()) {
        const auto& t = tasks_[selectedIndex_];
        appendLog(L"--- 选中任务: " + t.name + L" ---");
        appendLog(L"  源: " + t.sourcePath);
        appendLog(L"  目标: " + t.targetPath);
        appendLog(L"  模式: " + std::wstring(t.mode == BackupMode::Incremental ? L"增量" : L"全量"));
    }
}

void MainWindow::onDeleteTask() {
    if (selectedIndex_ < 0 || selectedIndex_ >= (int)tasks_.size()) return;
    const auto& t = tasks_[selectedIndex_];
    if (MessageBoxW(hwnd_, (L"确定删除任务 \"" + t.name + L"\"？").c_str(),
                    L"确认删除", MB_YESNO | MB_ICONQUESTION) != IDYES) return;

    tasks_.erase(tasks_.begin() + selectedIndex_);
    TaskStore::save(tasks_);
    selectedIndex_ = -1;
    refreshTaskList();
    appendLog(L"已删除任务: " + t.name);
}

// ============ 操作执行入口 ============
void MainWindow::onBackupNow() {
    if (busy_) { MessageBoxW(hwnd_, L"有任务正在执行，请稍候。", L"提示", MB_OK); return; }
    if (selectedIndex_ < 0) { MessageBoxW(hwnd_, L"请先选择一个任务。", L"提示", MB_OK); return; }

    const auto& task = tasks_[selectedIndex_];
    setBusy(true);
    setStatus(L"备份中...");
    appendLog(L"========== 开始备份: " + task.name + L" ==========");

    auto* param = new WorkerParam{this, task, L"", WorkerParam::Op::Backup};
    workerThread_ = CreateThread(nullptr, 0, workerThreadProc, param, 0, nullptr);
}

void MainWindow::onRestore() {
    if (busy_) { MessageBoxW(hwnd_, L"有任务正在执行，请稍候。", L"提示", MB_OK); return; }
    if (selectedIndex_ < 0) { MessageBoxW(hwnd_, L"请先选择一个任务。", L"提示", MB_OK); return; }

    std::wstring restorePath;
    if (!showInputDialog(L"恢复备份", L"请输入恢复目标目录:", L"D:\\Restored", restorePath)) return;

    const auto& task = tasks_[selectedIndex_];
    setBusy(true);
    setStatus(L"恢复中...");
    appendLog(L"========== 开始恢复: " + task.name + L" -> " + restorePath + L" ==========");

    auto* param = new WorkerParam{this, task, restorePath, WorkerParam::Op::Restore};
    workerThread_ = CreateThread(nullptr, 0, workerThreadProc, param, 0, nullptr);
}

void MainWindow::onVerify() {
    if (busy_) { MessageBoxW(hwnd_, L"有任务正在执行，请稍候。", L"提示", MB_OK); return; }
    if (selectedIndex_ < 0) { MessageBoxW(hwnd_, L"请先选择一个任务。", L"提示", MB_OK); return; }

    const auto& task = tasks_[selectedIndex_];
    setBusy(true);
    setStatus(L"校验中...");
    appendLog(L"========== 开始校验: " + task.name + L" ==========");

    auto* param = new WorkerParam{this, task, L"", WorkerParam::Op::Verify};
    workerThread_ = CreateThread(nullptr, 0, workerThreadProc, param, 0, nullptr);
}

void MainWindow::onCancel() {
    if (busy_) {
        cancelFlag_ = true;
        setStatus(L"正在取消...");
        appendLog(L"用户请求取消...");
    }
}

// ============ 后台线程 ============
DWORD WINAPI MainWindow::workerThreadProc(LPVOID param) {
    auto* p = (WorkerParam*)param;
    MainWindow* self = p->self;
    self->cancelFlag_ = false;

    try {
        if (p->op == WorkerParam::Op::Backup) {
            self->runBackup(p->task);
        } else if (p->op == WorkerParam::Op::Restore) {
            self->runRestore(p->task, p->restorePath);
        } else if (p->op == WorkerParam::Op::Verify) {
            self->runVerify(p->task);
        }
    } catch (...) {
        self->postLog(L"[错误] 后台线程发生未捕获异常");
    }

    delete p;
    return 0;
}

void MainWindow::runBackup(const BackupTask& task) {
    BackupConfig cfg = task.toConfig();
    BackupManager::Options opts;
    opts.cancelCheck = [this]() { return cancelFlag_; };
    opts.progress = [this](const std::wstring& path) {
        postLog(L"  处理: " + path);
    };

    BackupResult res = BackupManager::run(cfg, opts);

    postLog(L"---------- 备份结果 ----------");
    postLog(L"  状态: " + std::wstring(res.success ? L"成功" : (res.cancelled ? L"已取消" : L"失败")));
    postLog(L"  扫描: " + std::to_wstring(res.totalScanned) + L" 个文件");
    postLog(L"  写入: " + std::to_wstring(res.backedUp) + L" 个文件, " + std::to_wstring(res.totalBytes) + L" 字节");
    postLog(L"  新增: " + std::to_wstring(res.added) + L"  修改: " + std::to_wstring(res.modified) +
            L"  删除: " + std::to_wstring(res.deleted) + L"  失败: " + std::to_wstring(res.failed));
    for (const auto& err : res.errors) postLog(L"  [错误] " + err);
    for (const auto& warn : res.warnings) postLog(L"  [警告] " + warn);
    postLog(L"================================");

    PostMessageW(hwnd_, WM_BACKUP_DONE, res.success ? 1 : 0, 0);
}

void MainWindow::runRestore(const BackupTask& task, const std::wstring& restorePath) {
    RestoreConfig cfg;
    cfg.backupRoot = task.targetPath;
    cfg.restorePath = restorePath;
    cfg.overwrite = true;
    cfg.password = task.password;

    RestoreResult res = RestoreManager::run(cfg,
        [this]() { return cancelFlag_; },
        [this](const std::wstring& path) { postLog(L"  恢复: " + path); });

    postLog(L"---------- 恢复结果 ----------");
    postLog(L"  状态: " + std::wstring(res.success ? L"成功" : (res.cancelled ? L"已取消" : L"失败")));
    postLog(L"  恢复: " + std::to_wstring(res.restored) + L"  跳过: " + std::to_wstring(res.skipped) +
            L"  失败: " + std::to_wstring(res.failed));
    postLog(L"  Hash校验: " + std::to_wstring(res.verified) + L" 通过, " + std::to_wstring(res.hashMismatch) + L" 不一致");
    for (const auto& err : res.errors) postLog(L"  [错误] " + err);
    postLog(L"================================");

    PostMessageW(hwnd_, WM_RESTORE_DONE, res.success ? 1 : 0, 0);
}

void MainWindow::runVerify(const BackupTask& task) {
    VerifyOptions opts;
    opts.cancelCheck = [this]() { return cancelFlag_; };
    opts.progress = [this](const std::wstring& path) { postLog(L"  校验: " + path); };

    VerifyResult res = VerifyManager::run(task.targetPath, opts);

    postLog(L"---------- 校验结果 ----------");
    postLog(L"  总数: " + std::to_wstring(res.total));
    postLog(L"  通过: " + std::to_wstring(res.passed) + L"  缺失: " + std::to_wstring(res.missing) +
            L"  损坏: " + std::to_wstring(res.corrupted) + L"  残留: " + std::to_wstring(res.residual));
    postLog(L"  状态: " + std::wstring(res.success ? L"完整" : L"不完整"));
    for (const auto& err : res.errors) postLog(L"  [错误] " + err);
    postLog(L"================================");

    PostMessageW(hwnd_, WM_VERIFY_DONE, res.success ? 1 : 0, 0);
}

// ============ 日志与状态 ============
void MainWindow::appendLog(const std::wstring& text) {
    int len = GetWindowTextLengthW(logBox_);
    SendMessageW(logBox_, EM_SETSEL, len, len);
    SendMessageW(logBox_, EM_REPLACESEL, FALSE, (LPARAM)(text + L"\r\n").c_str());
    SendMessageW(logBox_, EM_SCROLLCARET, 0, 0);
}

void MainWindow::setStatus(const std::wstring& text) {
    SetWindowTextW(statusLabel_, text.c_str());
}

void MainWindow::setBusy(bool busy) {
    busy_ = busy;
    EnableWindow(GetDlgItem(hwnd_, IDC_BTN_BACKUP), !busy);
    EnableWindow(GetDlgItem(hwnd_, IDC_BTN_RESTORE), !busy);
    EnableWindow(GetDlgItem(hwnd_, IDC_BTN_VERIFY), !busy);
    EnableWindow(GetDlgItem(hwnd_, IDC_BTN_NEW), !busy);
    EnableWindow(GetDlgItem(hwnd_, IDC_BTN_DELETE), !busy);
    SendMessageW(progressBar_, PBM_SETPOS, busy ? 30 : 0, 0);
    if (busy) SendMessageW(progressBar_, PBM_SETMARQUEE, TRUE, 30);
    else SendMessageW(progressBar_, PBM_SETMARQUEE, FALSE, 0);
}

void MainWindow::postLog(const std::wstring& text) {
    size_t len = text.size() + 1;
    auto* buf = new wchar_t[len];
    wcscpy_s(buf, len, text.c_str());
    PostMessageW(hwnd_, WM_LOG_MSG, 0, (LPARAM)buf);
}

// ============ 新建任务对话框 ============
// 控件 ID
enum DlgCtrl : int {
    IDC_NAME = 2001,
    IDC_SOURCE = 2002,
    IDC_TARGET = 2003,
    IDC_MODE = 2004,
    IDC_ENCRYPT = 2005,
    IDC_PASSWORD = 2006,
    IDC_SNAPSHOTS = 2007,
    IDC_BROWSE_SRC = 2008,
    IDC_BROWSE_TGT = 2009,
};

struct NewTaskDlgData {
    BackupTask* outTask = nullptr;
    HWND hName = nullptr;
    HWND hSource = nullptr;
    HWND hTarget = nullptr;
    HWND hMode = nullptr;
    HWND hEncrypt = nullptr;
    HWND hPassword = nullptr;
    HWND hSnapshots = nullptr;
};

static INT_PTR CALLBACK newTaskDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* data = (NewTaskDlgData*)GetWindowLongPtrW(hDlg, GWLP_USERDATA);

    switch (msg) {
        case WM_INITDIALOG: {
            data = (NewTaskDlgData*)lParam;
            SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)data);

            // 创建控件
            int y = 10;
            auto createLabel = [&](const wchar_t* text, int cy) {
                CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE,
                    10, y, 80, 20, hDlg, nullptr, nullptr, nullptr);
                y += cy;
            };
            auto createEdit = [&](int id, int cy, const wchar_t* def = L"") {
                HWND h = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", def,
                    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                    95, y - cy + 2, 280, 22, hDlg, (HMENU)id, nullptr, nullptr);
                return h;
            };

            y = 15;
            createLabel(L"任务名称:", 28);
            data->hName = createEdit(IDC_NAME, 28);

            createLabel(L"源目录:", 28);
            data->hSource = createEdit(IDC_SOURCE, 28);
            CreateWindowExW(0, L"BUTTON", L"...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                380, y - 26, 30, 24, hDlg, (HMENU)IDC_BROWSE_SRC, nullptr, nullptr);

            createLabel(L"目标目录:", 28);
            data->hTarget = createEdit(IDC_TARGET, 28);
            CreateWindowExW(0, L"BUTTON", L"...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                380, y - 26, 30, 24, hDlg, (HMENU)IDC_BROWSE_TGT, nullptr, nullptr);

            createLabel(L"备份模式:", 28);
            data->hMode = CreateWindowExW(0, L"COMBOBOX", L"",
                WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                95, y - 26, 150, 100, hDlg, (HMENU)IDC_MODE, nullptr, nullptr);
            SendMessageW(data->hMode, CB_ADDSTRING, 0, (LPARAM)L"全量备份");
            SendMessageW(data->hMode, CB_ADDSTRING, 0, (LPARAM)L"增量备份");
            SendMessageW(data->hMode, CB_SETCURSEL, 0, 0);

            createLabel(L"加密:", 28);
            data->hEncrypt = CreateWindowExW(0, L"COMBOBOX", L"",
                WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                95, y - 26, 150, 100, hDlg, (HMENU)IDC_ENCRYPT, nullptr, nullptr);
            SendMessageW(data->hEncrypt, CB_ADDSTRING, 0, (LPARAM)L"不加密");
            SendMessageW(data->hEncrypt, CB_ADDSTRING, 0, (LPARAM)L"AES-256");
            SendMessageW(data->hEncrypt, CB_SETCURSEL, 0, 0);

            createLabel(L"密码:", 28);
            data->hPassword = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_PASSWORD,
                95, y - 26, 280, 22, hDlg, (HMENU)IDC_PASSWORD, nullptr, nullptr);
            EnableWindow(data->hPassword, FALSE);

            createLabel(L"保留快照:", 28);
            data->hSnapshots = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"0",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER,
                95, y - 26, 80, 22, hDlg, (HMENU)IDC_SNAPSHOTS, nullptr, nullptr);

            // 确定/取消按钮
            int btnY = y + 10;
            CreateWindowExW(0, L"BUTTON", L"确定", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                120, btnY, 80, 28, hDlg, (HMENU)IDOK, nullptr, nullptr);
            CreateWindowExW(0, L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                220, btnY, 80, 28, hDlg, (HMENU)IDCANCEL, nullptr, nullptr);

            return TRUE;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == IDC_ENCRYPT && HIWORD(wParam) == CBN_SELCHANGE) {
                int sel = (int)SendMessageW(data->hEncrypt, CB_GETCURSEL, 0, 0);
                EnableWindow(data->hPassword, sel == 1);
            }
            if (id == IDC_BROWSE_SRC || id == IDC_BROWSE_TGT) {
                BROWSEINFOW bi = {0};
                bi.hwndOwner = hDlg;
                bi.lpszTitle = (id == IDC_BROWSE_SRC) ? L"选择源目录" : L"选择目标目录";
                bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
                LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
                if (pidl) {
                    wchar_t path[MAX_PATH];
                    SHGetPathFromIDListW(pidl, path);
                    SetWindowTextW((id == IDC_BROWSE_SRC) ? data->hSource : data->hTarget, path);
                    CoTaskMemFree(pidl);
                }
            }
            if (id == IDOK) {
                wchar_t name[256], source[MAX_PATH], target[MAX_PATH], password[256], snapshots[16];
                GetWindowTextW(data->hName, name, 256);
                GetWindowTextW(data->hSource, source, MAX_PATH);
                GetWindowTextW(data->hTarget, target, MAX_PATH);
                GetWindowTextW(data->hPassword, password, 256);
                GetWindowTextW(data->hSnapshots, snapshots, 16);

                if (wcslen(name) == 0 || wcslen(source) == 0 || wcslen(target) == 0) {
                    MessageBoxW(hDlg, L"请填写任务名称、源目录和目标目录。", L"提示", MB_OK | MB_ICONWARNING);
                    return TRUE;
                }

                int encSel = (int)SendMessageW(data->hEncrypt, CB_GETCURSEL, 0, 0);
                if (encSel == 1 && wcslen(password) == 0) {
                    MessageBoxW(hDlg, L"加密模式下请输入密码。", L"提示", MB_OK | MB_ICONWARNING);
                    return TRUE;
                }

                data->outTask->name = name;
                data->outTask->sourcePath = source;
                data->outTask->targetPath = target;
                data->outTask->mode = (SendMessageW(data->hMode, CB_GETCURSEL, 0, 0) == 1)
                    ? BackupMode::Incremental : BackupMode::Full;
                data->outTask->encryption = (encSel == 1) ? "aes256" : "none";
                data->outTask->password = (encSel == 1) ? backup::wideToUtf8(password) : "";
                try { data->outTask->keepSnapshots = std::stoi(snapshots); } catch (...) { data->outTask->keepSnapshots = 0; }

                DestroyWindow(hDlg);
                return TRUE;
            }
            if (id == IDCANCEL) {
                data->outTask->name.clear();
                DestroyWindow(hDlg);
                return TRUE;
            }
        }
    }
    return FALSE;
}

bool MainWindow::showNewTaskDialog(BackupTask& outTask) {
    NewTaskDlgData data;
    data.outTask = &outTask;
    INT_PTR result = DialogBoxParamW(hInstance_, L"NewTaskDlg", hwnd_, newTaskDlgProc, (LPARAM)&data);
    return result == IDOK;
}

void MainWindow::onNewTask() {
    BackupTask task;
    NewTaskDlgData data;
    data.outTask = &task;

    // 用 CreateWindowEx 手动创建模态对话框（无需 .rc 资源）
    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE,
        L"NewTaskDlg", L"新建备份任务",
        WS_POPUPWINDOW | WS_CAPTION,
        CW_USEDEFAULT, CW_USEDEFAULT, 440, 400,
        hwnd_, nullptr, hInstance_, nullptr);

    if (!hDlg) {
        // fallback: 简单输入方式
        std::wstring name, source, target;
        if (!showInputDialog(L"新建任务", L"任务名称:", L"我的备份", name)) return;
        if (!showInputDialog(L"新建任务", L"源目录:", L"D:\\MyData", source)) return;
        if (!showInputDialog(L"新建任务", L"目标目录:", L"D:\\Backup", target)) return;
        task.name = name;
        task.sourcePath = source;
        task.targetPath = target;
        task.mode = BackupMode::Full;
        task.encryption = "none";
    } else {
        SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)&data);
        SendMessageW(hDlg, WM_INITDIALOG, 0, (LPARAM)&data);
        ShowWindow(hDlg, SW_SHOW);
        EnableWindow(hwnd_, FALSE);  // 模态：禁用父窗口

        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0)) {
            if (!IsDialogMessageW(hDlg, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (!IsWindow(hDlg)) break;
        }
        EnableWindow(hwnd_, TRUE);
        SetForegroundWindow(hwnd_);
        if (data.outTask->name.empty()) return;  // 用户取消
    }

    tasks_.push_back(task);
    TaskStore::save(tasks_);
    selectedIndex_ = (int)tasks_.size() - 1;
    refreshTaskList();
    appendLog(L"已创建任务: " + task.name);
}

// ============ 简单输入对话框 ============
struct InputDlgData {
    const wchar_t* title = nullptr;
    const wchar_t* prompt = nullptr;
    const wchar_t* defaultValue = nullptr;
    std::wstring* outValue = nullptr;
    HWND hEdit = nullptr;
};

static INT_PTR CALLBACK inputDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* data = (InputDlgData*)GetWindowLongPtrW(hDlg, GWLP_USERDATA);

    switch (msg) {
        case WM_INITDIALOG: {
            data = (InputDlgData*)lParam;
            SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)data);
            SetWindowTextW(hDlg, data->title);

            CreateWindowExW(0, L"STATIC", data->prompt, WS_CHILD | WS_VISIBLE,
                10, 10, 300, 20, hDlg, nullptr, nullptr, nullptr);
            data->hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", data->defaultValue,
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                10, 35, 360, 24, hDlg, nullptr, nullptr, nullptr);
            CreateWindowExW(0, L"BUTTON", L"确定", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                100, 70, 80, 28, hDlg, (HMENU)IDOK, nullptr, nullptr);
            CreateWindowExW(0, L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                200, 70, 80, 28, hDlg, (HMENU)IDCANCEL, nullptr, nullptr);

            SetFocus(data->hEdit);
            return FALSE;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK) {
                wchar_t buf[1024];
                GetWindowTextW(data->hEdit, buf, 1024);
                *data->outValue = buf;
                DestroyWindow(hDlg);
                return TRUE;
            }
            if (LOWORD(wParam) == IDCANCEL) {
                data->outValue->clear();
                DestroyWindow(hDlg);
                return TRUE;
            }
    }
    return FALSE;
}

bool MainWindow::showInputDialog(const std::wstring& title, const std::wstring& prompt,
                                   const std::wstring& defaultValue, std::wstring& outValue) {
    InputDlgData data;
    data.title = title.c_str();
    data.prompt = prompt.c_str();
    data.defaultValue = defaultValue.c_str();
    data.outValue = &outValue;

    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE,
        L"InputDlg", title.c_str(),
        WS_POPUPWINDOW | WS_CAPTION,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 150,
        hwnd_, nullptr, hInstance_, nullptr);

    if (!hDlg) return false;

    SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)&data);
    SendMessageW(hDlg, WM_INITDIALOG, 0, (LPARAM)&data);
    ShowWindow(hDlg, SW_SHOW);
    EnableWindow(hwnd_, FALSE);

    bool ok = false;
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!IsWindow(hDlg)) {
            // 检查是否是 IDOK 关闭的
            ok = !outValue.empty() || defaultValue.empty();
            break;
        }
    }
    EnableWindow(hwnd_, TRUE);
    SetForegroundWindow(hwnd_);
    return ok;
}

}  // namespace gui
}  // namespace backup
