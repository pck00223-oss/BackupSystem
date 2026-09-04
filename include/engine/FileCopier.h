// FileCopier.h - 文件复制器
// 职责：分块复制文件内容，支持取消检查；复制完成后恢复目标文件的修改时间。
// 不负责：Hash 计算、打包、业务判断。
#pragma once

#include <functional>
#include <string>

namespace backup {

class FileCopier {
public:
    using CancelCheck = std::function<bool()>;

    // 复制 src 到 dst。目标父目录必须已存在（由调用方保证）。
    // 成功返回 true；失败返回 false 并填充 errMsg（含取消）。
    static bool copyFile(const std::wstring& src,
                         const std::wstring& dst,
                         std::string* errMsg,
                         const CancelCheck& cancel = nullptr);
};

}  // namespace backup
