// FileCopier.cpp - 文件复制实现
#include "engine/FileCopier.h"

#include <atomic>
#include <vector>

#include "core/ResidualUtil.h"
#include "engine/FileSystem.h"

namespace backup {

// 全局计数器，保证同一进程内临时文件名唯一
static std::atomic<uint64_t> g_tmpCounter{0};

bool FileCopier::copyFile(const std::wstring& src,
                          const std::wstring& dst,
                          std::string* errMsg,
                          const CancelCheck& cancel) {
    FileHandle in = FileSystem::openRead(src, errMsg);
    if (!in.valid()) return false;

    // 原子写入：先写同目录唯一临时文件，成功后 MoveFileEx 原子替换目标。
    // 临时文件名用 "dst + .baktmp. + pid + . + counter"，不再用固定的 "dst + .baktmp"，
    // 避免与用户的合法文件（如 a.txt.baktmp）撞名并被 DeleteFileW 误删。
    const DWORD pid = ::GetCurrentProcessId();
    std::wstring tmp;
    FileHandle out;
    for (int attempt = 0; attempt < 100; ++attempt) {
        tmp = dst + tempSuffix() + L"." + std::to_wstring(pid) + L"." +
              std::to_wstring(g_tmpCounter.fetch_add(1));
        // overwrite=false 用 CREATE_NEW，已存在则失败并重试，绝不覆盖已存在的文件
        out = FileSystem::openWrite(tmp, /*overwrite=*/false, errMsg);
        if (out.valid()) break;
    }
    if (!out.valid()) {
        if (errMsg) *errMsg = "cannot create unique temp file after 100 attempts";
        return false;
    }

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
