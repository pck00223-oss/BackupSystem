// CompressedFile.cpp - 文件级分块压缩/解压实现
#include "core/CompressedFile.h"

#include <windows.h>

#include <cstring>
#include <fstream>
#include <vector>

#include "core/ICompressionAlgorithm.h"
#include "core/Logger.h"

namespace backup {

namespace {

constexpr size_t kBlockSize = 1024 * 1024;  // 1 MB
const char kMagic[4] = {'B', 'K', 'C', 'P'};

void writeU32(std::ofstream& o, uint32_t v) {
    const uint8_t b[4] = {static_cast<uint8_t>(v & 0xFF),
                          static_cast<uint8_t>((v >> 8) & 0xFF),
                          static_cast<uint8_t>((v >> 16) & 0xFF),
                          static_cast<uint8_t>((v >> 24) & 0xFF)};
    o.write(reinterpret_cast<const char*>(b), 4);
}
bool readU32(std::ifstream& in, uint32_t& v) {
    uint8_t b[4];
    in.read(reinterpret_cast<char*>(b), 4);
    if (in.gcount() != 4) return false;
    v = static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) |
        (static_cast<uint32_t>(b[2]) << 16) | (static_cast<uint32_t>(b[3]) << 24);
    return true;
}

// 在 dst 所在目录生成唯一临时文件，返回路径。
bool makeTemp(const std::wstring& dst, std::wstring& tmp) {
    const size_t pos = dst.find_last_of(L"\\/");
    const std::wstring dir = (pos == std::wstring::npos) ? L"." : dst.substr(0, pos);
    wchar_t buf[MAX_PATH];
    if (::GetTempFileNameW(dir.c_str(), L"cmp", 0, buf) == 0) return false;
    tmp = buf;
    return true;
}

}  // namespace

bool CompressedFile::compressFile(const std::wstring& src, const std::wstring& dst,
                                  const std::string& algorithm, std::string& err,
                                  const CancelCheck& cancel) {
    std::ifstream ifs(src.c_str(), std::ios::binary);
    if (!ifs) { err = "cannot open source file"; return false; }

    auto codec = createCompressionAlgorithm(algorithm);
    if (!codec) { err = "unknown compression algorithm: " + algorithm; return false; }

    std::wstring tmp;
    if (!makeTemp(dst, tmp)) { err = "cannot create temp file"; return false; }

    std::ofstream ofs(tmp.c_str(), std::ios::binary | std::ios::trunc);
    if (!ofs) { err = "cannot open temp file for write"; ::DeleteFileW(tmp.c_str()); return false; }

    ofs.write(kMagic, 4);
    writeU32(ofs, 0);  // 块数占位，结束后回填

    std::vector<uint8_t> readBuf(kBlockSize);
    uint32_t blockCount = 0;

    while (ifs) {
        ifs.read(reinterpret_cast<char*>(readBuf.data()),
                 static_cast<std::streamsize>(kBlockSize));
        const std::streamsize got = ifs.gcount();
        if (got == 0) break;
        if (cancel && cancel()) {
            ofs.close();
            ::DeleteFileW(tmp.c_str());
            err = "cancelled";
            return false;
        }
        const std::vector<uint8_t> comp =
            codec->compress(readBuf.data(), static_cast<size_t>(got));
        writeU32(ofs, static_cast<uint32_t>(got));
        writeU32(ofs, static_cast<uint32_t>(comp.size()));
        if (!comp.empty())
            ofs.write(reinterpret_cast<const char*>(comp.data()),
                      static_cast<std::streamsize>(comp.size()));
        ++blockCount;
    }

    ofs.seekp(4);
    writeU32(ofs, blockCount);
    ofs.flush();
    if (!ofs) {
        ofs.close();
        ::DeleteFileW(tmp.c_str());
        err = "write compressed file failed";
        return false;
    }
    ofs.close();

    if (!::MoveFileExW(tmp.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        err = "atomic replace failed (error " + std::to_string(::GetLastError()) + ")";
        ::DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

bool CompressedFile::decompressFile(const std::wstring& src, const std::wstring& dst,
                                    const std::string& algorithm, std::string& err,
                                    const CancelCheck& cancel) {
    std::ifstream ifs(src.c_str(), std::ios::binary);
    if (!ifs) { err = "cannot open compressed file"; return false; }

    char magic[4];
    ifs.read(magic, 4);
    if (ifs.gcount() != 4 || std::memcmp(magic, kMagic, 4) != 0) {
        err = "invalid compressed file magic";
        return false;
    }

    uint32_t blockCount = 0;
    if (!readU32(ifs, blockCount)) { err = "truncated block count"; return false; }

    auto codec = createCompressionAlgorithm(algorithm);
    if (!codec) { err = "unknown compression algorithm: " + algorithm; return false; }

    std::wstring tmp;
    if (!makeTemp(dst, tmp)) { err = "cannot create temp file"; return false; }

    std::ofstream ofs(tmp.c_str(), std::ios::binary | std::ios::trunc);
    if (!ofs) { err = "cannot open temp file for write"; ::DeleteFileW(tmp.c_str()); return false; }

    for (uint32_t b = 0; b < blockCount; ++b) {
        uint32_t origSize = 0, compSize = 0;
        if (!readU32(ifs, origSize) || !readU32(ifs, compSize)) {
            err = "truncated block header";
            ofs.close();
            ::DeleteFileW(tmp.c_str());
            return false;
        }
        std::vector<uint8_t> comp(compSize);
        if (compSize > 0) {
            ifs.read(reinterpret_cast<char*>(comp.data()),
                     static_cast<std::streamsize>(compSize));
            if (static_cast<uint32_t>(ifs.gcount()) != compSize) {
                err = "truncated block data";
                ofs.close();
                ::DeleteFileW(tmp.c_str());
                return false;
            }
        }
        if (cancel && cancel()) {
            ofs.close();
            ::DeleteFileW(tmp.c_str());
            err = "cancelled";
            return false;
        }
        bool ok = false;
        const std::vector<uint8_t> plain =
            codec->decompress(comp.data(), comp.size(), &ok);
        if (!ok || plain.size() != origSize) {
            err = "decompression failed (corrupted block)";
            ofs.close();
            ::DeleteFileW(tmp.c_str());
            return false;
        }
        ofs.write(reinterpret_cast<const char*>(plain.data()),
                  static_cast<std::streamsize>(plain.size()));
    }

    ofs.flush();
    if (!ofs) {
        ofs.close();
        ::DeleteFileW(tmp.c_str());
        err = "write decompressed file failed";
        return false;
    }
    ofs.close();

    if (!::MoveFileExW(tmp.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        err = "atomic replace failed (error " + std::to_string(::GetLastError()) + ")";
        ::DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

}  // namespace backup
