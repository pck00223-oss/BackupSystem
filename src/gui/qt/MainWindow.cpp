// MainWindow.cpp - Qt 主窗口实现
#include "gui/qt/MainWindow.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "gui/qt/TaskDialog.h"
#include "gui/qt/Worker.h"

namespace {
QString escapeHtml(const QString& s) {
    return s.toHtmlEscaped();
}
}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("BackupSystem - 本地数据备份与恢复"));
    resize(1180, 760);
    tasks_ = backup::gui::TaskStore::load();
    buildUi();
    applyStyle();
    refreshTable();
    statusLabel_->setText(QStringLiteral("就绪。共 %1 个任务。").arg(tasks_.size()));
}

void MainWindow::buildUi() {
    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(14);

    // ---- 顶部标题区 ----
    auto* header = new QFrame(central);
    header->setObjectName("header");
    auto* hLayout = new QVBoxLayout(header);
    auto* title = new QLabel(QStringLiteral("BackupSystem"), header);
    title->setObjectName("appTitle");
    auto* subtitle = new QLabel(
        QStringLiteral("本地数据备份 · 增量检测 · 快照 · 加密 · 压缩"), header);
    subtitle->setObjectName("appSubtitle");
    hLayout->addWidget(title);
    hLayout->addWidget(subtitle);
    root->addWidget(header);

    // ---- 主体：左右可拖拽分栏 ----
    auto* splitter = new QSplitter(Qt::Horizontal, central);

    // 左：任务面板
    auto* taskPanel = new QFrame(splitter);
    taskPanel->setObjectName("card");
    auto* taskLay = new QVBoxLayout(taskPanel);
    auto* taskHead = new QLabel(QStringLiteral("备份任务"), taskPanel);
    taskHead->setObjectName("panelTitle");
    taskLay->addWidget(taskHead);

    auto* btnGrid = new QGridLayout();
    btnGrid->setSpacing(8);
    newBtn_ = new QPushButton(QStringLiteral("新建任务"), taskPanel);
    editBtn_ = new QPushButton(QStringLiteral("编辑"), taskPanel);
    delBtn_ = new QPushButton(QStringLiteral("删除"), taskPanel);
    backupBtn_ = new QPushButton(QStringLiteral("立即备份"), taskPanel);
    backupBtn_->setObjectName("primary");
    restoreBtn_ = new QPushButton(QStringLiteral("恢复"), taskPanel);
    verifyBtn_ = new QPushButton(QStringLiteral("校验"), taskPanel);
    cancelBtn_ = new QPushButton(QStringLiteral("取消"), taskPanel);
    cancelBtn_->setObjectName("danger");
    btnGrid->addWidget(newBtn_, 0, 0);
    btnGrid->addWidget(editBtn_, 0, 1);
    btnGrid->addWidget(delBtn_, 0, 2);
    btnGrid->addWidget(backupBtn_, 1, 0);
    btnGrid->addWidget(restoreBtn_, 1, 1);
    btnGrid->addWidget(verifyBtn_, 1, 2);
    btnGrid->addWidget(cancelBtn_, 1, 3);
    taskLay->addLayout(btnGrid);

    table_ = new QTableWidget(0, 6, taskPanel);
    table_->setHorizontalHeaderLabels(
        {QStringLiteral("任务名称"), QStringLiteral("源目录"),
         QStringLiteral("目标目录"), QStringLiteral("模式"),
         QStringLiteral("压缩"), QStringLiteral("加密")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->setAlternatingRowColors(true);
    taskLay->addWidget(table_);

    // 右：监控面板
    auto* monPanel = new QFrame(splitter);
    monPanel->setObjectName("card");
    auto* monLay = new QVBoxLayout(monPanel);
    auto* monHead = new QLabel(QStringLiteral("运行监控"), monPanel);
    monHead->setObjectName("panelTitle");
    monLay->addWidget(monHead);

    progress_ = new QProgressBar(monPanel);
    progress_->setRange(0, 1);
    progress_->setValue(1);
    progress_->setTextVisible(false);
    monLay->addWidget(progress_);

    statusLabel_ = new QLabel(QStringLiteral("就绪"), monPanel);
    statusLabel_->setObjectName("statusLabel");
    monLay->addWidget(statusLabel_);

    logView_ = new QPlainTextEdit(monPanel);
    logView_->setReadOnly(true);
    logView_->setObjectName("logView");
    logView_->setFont(QFont("Consolas", 9));
    monLay->addWidget(logView_);

    splitter->addWidget(taskPanel);
    splitter->addWidget(monPanel);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    root->addWidget(splitter, 1);

    setCentralWidget(central);

    // ---- 信号连接 ----
    connect(newBtn_, &QPushButton::clicked, this, &MainWindow::newTask);
    connect(editBtn_, &QPushButton::clicked, this, &MainWindow::editTask);
    connect(delBtn_, &QPushButton::clicked, this, &MainWindow::deleteTask);
    connect(backupBtn_, &QPushButton::clicked, this, &MainWindow::backupNow);
    connect(restoreBtn_, &QPushButton::clicked, this, &MainWindow::restoreTask);
    connect(verifyBtn_, &QPushButton::clicked, this, &MainWindow::verifyTask);
    connect(cancelBtn_, &QPushButton::clicked, this, &MainWindow::cancelTask);
    connect(table_, &QTableWidget::itemSelectionChanged, this, []() {});
}

void MainWindow::applyStyle() {
    setStyleSheet(QStringLiteral(R"(
        QMainWindow, QWidget { background: #F7F9FC; color: #334155;
            font-family: "Microsoft YaHei"; font-size: 13px; }
        QLabel { background: transparent; }
        #header { background: #0B1220; border-radius: 12px; padding: 6px; }
        #appTitle { color: #FFFFFF; font-size: 26px; font-weight: 800; }
        #appSubtitle { color: #93C5FD; font-size: 13px; }
        #card { background: #FFFFFF; border: 1px solid #E2E8F0;
            border-radius: 12px; }
        #panelTitle { color: #0F172A; font-size: 16px; font-weight: 700;
            padding-bottom: 4px; }
        #statusLabel { color: #64748B; }
        QPushButton { background: #F1F5F9; color: #0F172A;
            border: 1px solid #CBD5E1; border-radius: 8px;
            padding: 7px 12px; }
        QPushButton:hover { background: #E2E8F0; }
        QPushButton:disabled { color: #94A3B8; background: #F8FAFC; }
        QPushButton#primary { background: #2563EB; color: #FFFFFF;
            border: none; font-weight: 600; }
        QPushButton#primary:hover { background: #1D4ED8; }
        QPushButton#danger { background: #FFFFFF; color: #DC2626;
            border: 1px solid #FCA5A5; }
        QPushButton#danger:hover { background: #FEF2F2; }
        QTableWidget { background: #FFFFFF; border: 1px solid #E2E8F0;
            border-radius: 8px; gridline-color: #EEF2F7; }
        QTableWidget::item { padding: 6px; }
        QHeaderView::section { background: #0F172A; color: #FFFFFF;
            border: none; padding: 8px; font-weight: 600; }
        QTableWidget { alternate-background-color: #F8FAFC; }
        #logView { background: #0B1220; border-radius: 8px; color: #CBD5E1; }
        QProgressBar { background: #E2E8F0; border-radius: 6px;
            height: 8px; }
        QProgressBar::chunk { background: #2563EB; border-radius: 6px; }
    )"));
}

void MainWindow::refreshTable() {
    table_->setRowCount(0);
    for (const auto& t : tasks_) {
        const int row = table_->rowCount();
        table_->insertRow(row);
        auto make = [](const QString& s) {
            auto* it = new QTableWidgetItem(s);
            it->setToolTip(s);
            return it;
        };
        table_->setItem(row, 0, make(QString::fromStdWString(t.name)));
        table_->setItem(row, 1, make(QString::fromStdWString(t.sourcePath)));
        table_->setItem(row, 2, make(QString::fromStdWString(t.targetPath)));
        table_->setItem(row, 3,
                        make(t.mode == backup::BackupMode::Incremental
                                 ? QStringLiteral("增量")
                                 : QStringLiteral("全量")));
        QString comp = QStringLiteral("不压缩");
        if (t.compression == "huffman") comp = QStringLiteral("Huffman");
        else if (t.compression == "rle") comp = QStringLiteral("RLE");
        table_->setItem(row, 4, make(comp));
        QString enc = QStringLiteral("不加密");
        if (t.encryption == "aes256") enc = QStringLiteral("AES-256");
        else if (t.encryption == "3des") enc = QStringLiteral("3DES");
        table_->setItem(row, 5, make(enc));
    }
    if (!tasks_.empty()) table_->selectRow(0);
}

int MainWindow::selectedRow() const {
    const QList<QTableWidgetSelectionRange> ranges = table_->selectedRanges();
    if (ranges.isEmpty()) return -1;
    return ranges.first().topRow();
}

void MainWindow::persistTasks() {
    backup::gui::TaskStore::save(tasks_);
}

void MainWindow::setBusy(bool busy) {
    busy_ = busy;
    for (QPushButton* b : {newBtn_, editBtn_, delBtn_, backupBtn_, restoreBtn_,
                           verifyBtn_}) {
        b->setEnabled(!busy);
    }
    cancelBtn_->setEnabled(busy);
    if (busy) {
        progress_->setRange(0, 0);  // 忙碌滚动动画
    } else {
        progress_->setRange(0, 1);
        progress_->setValue(1);
    }
}

void MainWindow::appendLog(const QString& text, int level) {
    static const char* colors[] = {"#CBD5E1", "#4ADE80", "#FBBF24", "#F87171"};
    const QString color = QString::fromLatin1(colors[level]);
    logView_->appendHtml(
        QStringLiteral("<span style='color:%1;'>%2</span>")
            .arg(color, escapeHtml(text)));
}

void MainWindow::onTaskFinished(bool ok, const QString& summary) {
    setBusy(false);
    statusLabel_->setText(summary + (ok ? QStringLiteral("（完成）")
                                        : QStringLiteral("（已停止）")));
    if (worker_) {
        worker_->wait();
        worker_->deleteLater();
        worker_ = nullptr;
    }
}

// ---------------- 任务管理 ----------------

void MainWindow::newTask() {
    TaskDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;
    backup::gui::BackupTask task;
    if (!dlg.result(task)) return;
    tasks_.push_back(task);
    persistTasks();
    refreshTable();
    table_->selectRow(static_cast<int>(tasks_.size()) - 1);
    appendLog(QStringLiteral("已创建任务: %1")
                  .arg(QString::fromStdWString(task.name)),
              Worker::Success);
}

void MainWindow::editTask() {
    const int row = selectedRow();
    if (row < 0) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("请先选择一个任务。"));
        return;
    }
    TaskDialog dlg(this);
    dlg.setTask(tasks_[row]);
    if (dlg.exec() != QDialog::Accepted) return;
    backup::gui::BackupTask task;
    if (!dlg.result(task)) return;
    tasks_[row] = task;
    persistTasks();
    refreshTable();
    table_->selectRow(row);
    appendLog(QStringLiteral("已更新任务: %1")
                  .arg(QString::fromStdWString(task.name)),
              Worker::Info);
}

void MainWindow::deleteTask() {
    const int row = selectedRow();
    if (row < 0) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("请先选择一个任务。"));
        return;
    }
    const QString name = QString::fromStdWString(tasks_[row].name);
    if (QMessageBox::question(
            this, QStringLiteral("确认删除"),
            QStringLiteral("确定删除任务「%1」吗？（备份数据不会被删除）")
                .arg(name)) != QMessageBox::Yes) {
        return;
    }
    tasks_.erase(tasks_.begin() + row);
    persistTasks();
    refreshTable();
    appendLog(QStringLiteral("已删除任务: %1").arg(name), Worker::Warn);
}

// ---------------- 执行操作 ----------------

void MainWindow::backupNow() {
    const int row = selectedRow();
    if (row < 0) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("请先选择一个任务。"));
        return;
    }
    worker_ = new Worker(this);
    worker_->configureBackup(tasks_[row]);
    connect(worker_, &Worker::logMessage, this, &MainWindow::appendLog);
    connect(worker_, &Worker::taskFinished, this, &MainWindow::onTaskFinished);
    setBusy(true);
    statusLabel_->setText(QStringLiteral("备份中..."));
    appendLog(QStringLiteral("========== 开始备份: %1 ==========")
                  .arg(QString::fromStdWString(tasks_[row].name)),
              Worker::Info);
    worker_->start();
}

void MainWindow::restoreTask() {
    const int row = selectedRow();
    if (row < 0) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("请先选择一个任务。"));
        return;
    }

    // 恢复设置对话框：目标目录 + 覆盖策略。
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("恢复设置"));
    dlg.setMinimumWidth(460);
    auto* lay = new QVBoxLayout(&dlg);
    auto* pathEdit = new QLineEdit(&dlg);
    auto* browse = new QPushButton(QStringLiteral("浏览..."), &dlg);
    auto* pathRow = new QHBoxLayout();
    pathRow->addWidget(pathEdit);
    pathRow->addWidget(browse);
    lay->addWidget(new QLabel(QStringLiteral("恢复目标目录:"), &dlg));
    lay->addLayout(pathRow);
    auto* overwrite = new QCheckBox(
        QStringLiteral("覆盖已存在且内容不同的文件"), &dlg);
    overwrite->setChecked(true);
    lay->addWidget(overwrite);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok |
                                         QDialogButtonBox::Cancel,
                                     &dlg);
    lay->addWidget(box);
    connect(browse, &QPushButton::clicked, &dlg, [&]() {
        const QString dir = QFileDialog::getExistingDirectory(
            &dlg, QStringLiteral("选择恢复目标目录"));
        if (!dir.isEmpty()) pathEdit->setText(dir);
    });
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted || pathEdit->text().trimmed().isEmpty())
        return;

    const std::wstring restorePath = pathEdit->text().trimmed().toStdWString();
    worker_ = new Worker(this);
    worker_->configureRestore(tasks_[row], restorePath, overwrite->isChecked());
    connect(worker_, &Worker::logMessage, this, &MainWindow::appendLog);
    connect(worker_, &Worker::taskFinished, this, &MainWindow::onTaskFinished);
    setBusy(true);
    statusLabel_->setText(QStringLiteral("恢复中..."));
    appendLog(QStringLiteral("========== 开始恢复: %1 -> %2 ==========")
                  .arg(QString::fromStdWString(tasks_[row].name),
                       pathEdit->text()),
              Worker::Info);
    worker_->start();
}

void MainWindow::verifyTask() {
    const int row = selectedRow();
    if (row < 0) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("请先选择一个任务。"));
        return;
    }
    worker_ = new Worker(this);
    worker_->configureVerify(tasks_[row]);
    connect(worker_, &Worker::logMessage, this, &MainWindow::appendLog);
    connect(worker_, &Worker::taskFinished, this, &MainWindow::onTaskFinished);
    setBusy(true);
    statusLabel_->setText(QStringLiteral("校验中..."));
    appendLog(QStringLiteral("========== 开始校验: %1 ==========")
                  .arg(QString::fromStdWString(tasks_[row].name)),
              Worker::Info);
    worker_->start();
}

void MainWindow::cancelTask() {
    if (worker_ && busy_) {
        worker_->requestCancel();
        statusLabel_->setText(QStringLiteral("正在取消..."));
        appendLog(QStringLiteral("用户请求取消..."), Worker::Warn);
    }
}
