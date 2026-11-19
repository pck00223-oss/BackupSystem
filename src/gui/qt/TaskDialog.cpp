// TaskDialog.cpp - 新建/编辑任务对话框实现
#include "gui/qt/TaskDialog.h"

#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

TaskDialog::TaskDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("新建备份任务"));
    setMinimumWidth(480);

    nameEdit_ = new QLineEdit(this);
    sourceEdit_ = new QLineEdit(this);
    targetEdit_ = new QLineEdit(this);

    auto* srcRow = new QHBoxLayout();
    srcRow->addWidget(sourceEdit_);
    auto* srcBrowse = new QPushButton(QStringLiteral("浏览..."), this);
    srcRow->addWidget(srcBrowse);

    auto* tgtRow = new QHBoxLayout();
    tgtRow->addWidget(targetEdit_);
    auto* tgtBrowse = new QPushButton(QStringLiteral("浏览..."), this);
    tgtRow->addWidget(tgtBrowse);

    modeCombo_ = new QComboBox(this);
    modeCombo_->addItem(QStringLiteral("全量备份"));
    modeCombo_->addItem(QStringLiteral("增量备份"));

    compressCombo_ = new QComboBox(this);
    compressCombo_->addItem(QStringLiteral("不压缩"));
    compressCombo_->addItem(QStringLiteral("Huffman 压缩"));
    compressCombo_->addItem(QStringLiteral("RLE 压缩"));

    encryptCombo_ = new QComboBox(this);
    encryptCombo_->addItem(QStringLiteral("不加密"));
    encryptCombo_->addItem(QStringLiteral("AES-256"));
    encryptCombo_->addItem(QStringLiteral("3DES"));

    passwordEdit_ = new QLineEdit(this);
    passwordEdit_->setEchoMode(QLineEdit::Password);
    passwordEdit_->setEnabled(false);

    keepSpin_ = new QSpinBox(this);
    keepSpin_->setRange(0, 999);
    keepSpin_->setValue(0);

    auto* form = new QFormLayout();
    form->addRow(QStringLiteral("任务名称:"), nameEdit_);
    form->addRow(QStringLiteral("源目录:"), srcRow);
    form->addRow(QStringLiteral("目标目录:"), tgtRow);
    form->addRow(QStringLiteral("备份模式:"), modeCombo_);
    form->addRow(QStringLiteral("压缩:"), compressCombo_);
    form->addRow(QStringLiteral("加密:"), encryptCombo_);
    form->addRow(QStringLiteral("密码:"), passwordEdit_);
    form->addRow(QStringLiteral("保留快照:"), keepSpin_);

    auto* buttons = new QHBoxLayout();
    buttons->addStretch();
    auto* okBtn = new QPushButton(QStringLiteral("确定"), this);
    auto* cancelBtn = new QPushButton(QStringLiteral("取消"), this);
    okBtn->setDefault(true);
    buttons->addWidget(okBtn);
    buttons->addWidget(cancelBtn);

    auto* root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addLayout(buttons);

    connect(srcBrowse, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(
            this, QStringLiteral("选择源目录"), sourceEdit_->text());
        if (!dir.isEmpty()) sourceEdit_->setText(dir);
    });
    connect(tgtBrowse, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(
            this, QStringLiteral("选择目标目录"), targetEdit_->text());
        if (!dir.isEmpty()) targetEdit_->setText(dir);
    });
    connect(encryptCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TaskDialog::onEncryptChanged);
    connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
}

void TaskDialog::onEncryptChanged(int index) {
    passwordEdit_->setEnabled(index != 0);
}

void TaskDialog::setTask(const backup::gui::BackupTask& task) {
    setWindowTitle(QStringLiteral("编辑备份任务"));
    nameEdit_->setText(QString::fromStdWString(task.name));
    sourceEdit_->setText(QString::fromStdWString(task.sourcePath));
    targetEdit_->setText(QString::fromStdWString(task.targetPath));
    modeCombo_->setCurrentIndex(task.mode == backup::BackupMode::Incremental ? 1 : 0);
    if (task.compression == "huffman") {
        compressCombo_->setCurrentIndex(1);
    } else if (task.compression == "rle") {
        compressCombo_->setCurrentIndex(2);
    } else {
        compressCombo_->setCurrentIndex(0);
    }
    if (task.encryption == "aes256") {
        encryptCombo_->setCurrentIndex(1);
    } else if (task.encryption == "3des") {
        encryptCombo_->setCurrentIndex(2);
    } else {
        encryptCombo_->setCurrentIndex(0);
    }
    passwordEdit_->setText(QString::fromStdString(task.password));
    keepSpin_->setValue(task.keepSnapshots);
}

bool TaskDialog::result(backup::gui::BackupTask& out) const {
    if (nameEdit_->text().trimmed().isEmpty() ||
        sourceEdit_->text().trimmed().isEmpty() ||
        targetEdit_->text().trimmed().isEmpty()) {
        QMessageBox::warning(nullptr, QStringLiteral("提示"),
                             QStringLiteral("请填写任务名称、源目录和目标目录。"));
        return false;
    }
    const int encSel = encryptCombo_->currentIndex();
    if (encSel != 0 && passwordEdit_->text().isEmpty()) {
        QMessageBox::warning(nullptr, QStringLiteral("提示"),
                             QStringLiteral("加密模式下请输入密码。"));
        return false;
    }

    out.name = nameEdit_->text().toStdWString();
    out.sourcePath = sourceEdit_->text().trimmed().toStdWString();
    out.targetPath = targetEdit_->text().trimmed().toStdWString();
    out.mode = modeCombo_->currentIndex() == 1
                   ? backup::BackupMode::Incremental
                   : backup::BackupMode::Full;
    const int compSel = compressCombo_->currentIndex();
    out.compression = (compSel == 1) ? "huffman" : (compSel == 2) ? "rle" : "none";
    out.encryption = (encSel == 1) ? "aes256" : (encSel == 2) ? "3des" : "none";
    out.password = passwordEdit_->text().toUtf8().toStdString();
    out.keepSnapshots = keepSpin_->value();
    return true;
}
