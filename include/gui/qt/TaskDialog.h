// TaskDialog.h - 新建/编辑备份任务对话框
// 职责：收集任务名称、源/目标目录、备份模式、加密方式与密码、保留快照数。
// 对应 Win32 版 NewTaskDlg，字段与校验规则保持一致。
#pragma once

#include <QDialog>

#include "gui/TaskStore.h"

class QLineEdit;
class QComboBox;
class QSpinBox;

class TaskDialog : public QDialog {
    Q_OBJECT
public:
    explicit TaskDialog(QWidget* parent = nullptr);

    // 编辑模式：用已有任务填充控件。
    void setTask(const backup::gui::BackupTask& task);

    // 读取控件生成任务；控件校验不通过时返回 false。
    bool result(backup::gui::BackupTask& out) const;

private slots:
    void onEncryptChanged(int index);

private:
    QLineEdit* nameEdit_ = nullptr;
    QLineEdit* sourceEdit_ = nullptr;
    QLineEdit* targetEdit_ = nullptr;
    QComboBox* modeCombo_ = nullptr;
    QComboBox* compressCombo_ = nullptr;
    QComboBox* encryptCombo_ = nullptr;
    QLineEdit* passwordEdit_ = nullptr;
    QSpinBox* keepSpin_ = nullptr;
};
