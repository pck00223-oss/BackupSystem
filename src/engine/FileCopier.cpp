// FileCopier.cpp - 文件复制实现
#include "engine/FileCopier.h"

#include <vector>

#include "engine/FileSystem.h"

namespace backup {

bool FileCopier::copyFile(const std::wstring& src,
                          const std::wstring& dst,
                          std::string* errMsg,
                          const CancelCheck& cancel) {
    FileHandle in = FileSystem::openRead(src, errMsg);
    if (!in.valid()) return false;

    FileHandle out = FileSystem::openWrite(dst, /*overwrite=*/true, errMsg);
    if (!out.valid()) return false;

    std::vector<uint8_t> buf(64 * 1024);
    for (;;) {
        if (cancel && cancel()) {
            if (errMsg) *errMsg = "cancelled";
            return false;
        }
        DWORD readCount = 0;
        if (!FileSystem::read(in.get(), buf.data(), static_cast<DWORD>(buf.size()), readCount, errMsg)) {
            return false;
        }
        if (readCount == 0) break;
        DWORD writtenCount = 0;
        if (!FileSystem::write(out.get(), buf.data(), readCount, writtenCount, errMsg)) {
            return false;
        }
    }

    ::FlushFileBuffers(out.get());

    // 复制完成后恢复目标文件的修改时间，尽量保持元数据一致。
    FileInfo srcInfo;
    if (FileSystem::getFileInfo(src, srcInfo) && srcInfo.modifiedTime != 0) {
        FileSystem::setFileTimes(dst, 0, srcInfo.modifiedTime);
    }
    return true;
}

}  // namespace backup
