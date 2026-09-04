// BackupTask.cpp - 可取消备份任务实现
#include "app/BackupTask.h"

#include "business/BackupManager.h"

namespace backup {

BackupResult BackupTask::run() const {
    BackupManager::Options opts;
    opts.cancelCheck = [this]() { return isCancelled(); };
    return BackupManager::run(config_, opts);
}

}  // namespace backup
