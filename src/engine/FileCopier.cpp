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

    // 原子写入：先写同目录临时文件，成功后 MoveFileEx 原子替换目标。
    // 避免写入中途失败/取消时留下截断文件，或覆盖恢复时先删后写导致原文件丢失。
    const std::wstring tmp = dst + L".baktmp";
    // 清理可能残留的临时文件（上次中断留下的）
    ::DeleteFileW(tmp.c_str());

    FileHandle out = FileSystem::openWrite(tmp, /*overwrite=*/true, errMsg);
    if (!out.valid()) return false;

    bool writeOk = true;
    std::vector<uint8_t> buf(64 * 1024);
    for (;;) {
        if (cancel && cancel()) {
            if (errMsg) *errMsg = "cancelled";
            writeOk = false;
            break;
        }
        DWORD readCount = 0;
        if (!FileSystem::read(in.get(), buf.data(), static_cast<DWORD>(buf.size()), readCount, errMsg)) {
            writeOk = false;
            break;
        }
        if (readCount == 0) break;
        DWORD writtenCount = 0;
        if (!FileSystem::write(out.get(), buf.data(), readCount, writtenCount, errMsg)) {
            writeOk = false;
            break;
        }
    }

    if (writeOk) {
        ::FlushFileBuffers(out.get());
    }
    out.close();  // 关闭临时文件句柄，MoveFileEx 才能替换

    if (!writeOk) {
        ::DeleteFileW(tmp.c_str());  // 写入失败/取消，清理临时文件
        return false;
    }

    // 原子替换：同卷 MoveFileEx 只更新目录项，不会留下中间状态。
    if (!::MoveFileExW(tmp.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        const DWORD err = ::GetLastError();
        if (errMsg) *errMsg = "atomic replace failed (error " + std::to_string(err) + ")";
        ::DeleteFileW(tmp.c_str());
        return false;
    }

    // 复制完成后恢复目标文件的修改时间，尽量保持元数据一致。
    FileInfo srcInfo;
    if (FileSystem::getFileInfo(src, srcInfo) && srcInfo.modifiedTime != 0) {
        FileSystem::setFileTimes(dst, 0, srcInfo.modifiedTime);
    }
    return true;
}

}  // namespace backup
