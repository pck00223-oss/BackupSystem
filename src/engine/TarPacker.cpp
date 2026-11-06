// TarPacker.cpp - TAR 打包/解包实现（ustar）
#include "engine/TarPacker.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <utility>

#include "core/Utf.h"
#include "engine/FileSystem.h"

namespace backup {

namespace {

constexpr size_t kBlock = 512;
constexpr size_t kNameLen = 100;
constexpr size_t kPrefixLen = 155;
constexpr size_t kLinkLen = 100;

// ustar 原始 512 字节头（对应课程 PPT 中的 union Record）。
struct TarHeader {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char padding[12];
};
static_assert(sizeof(TarHeader) == 512, "TarHeader must be 512 bytes");

// 把数值写入八进制 ASCII 字段（末尾留 NUL，其余右对齐补前导 0）。
void writeOctal(char* field, size_t fieldLen, uint64_t value) {
    // 保留最后一个字节给 NUL
    std::memset(field, '0', fieldLen);
    field[fieldLen - 1] = '\0';
    // 从右向左填八进制数字
    size_t pos = fieldLen - 2;
    if (value == 0) {
        field[pos] = '0';
        return;
    }
    while (value > 0 && pos != static_cast<size_t>(-1)) {
        field[pos] = static_cast<char>('0' + (value & 7));
        value >>= 3;
        if (pos == 0) break;
        --pos;
    }
}

// 把字符串写入定长字段（不足补 NUL，超长截断）。
void writeString(char* field, size_t fieldLen, const std::string& s) {
    const size_t n = std::min(s.size(), fieldLen);
    std::memcpy(field, s.data(), n);
    if (n < fieldLen) std::memset(field + n, 0, fieldLen - n);
}

// 计算并填充校验和：把 chksum 字段当 8 个空格，对 512 字节全部无符号求和。
void fillChecksum(TarHeader& h) {
    std::memset(h.chksum, ' ', sizeof(h.chksum));
    unsigned int sum = 0;
    const auto* bytes = reinterpret_cast<const unsigned char*>(&h);
    for (size_t i = 0; i < sizeof(TarHeader); ++i) sum += bytes[i];
    // 6 位八进制 + NUL + 空格
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%06o", sum);
    std::memcpy(h.chksum, buf, 6);
    h.chksum[6] = '\0';
    h.chksum[7] = ' ';
}

// 把归档内路径（UTF-8）拆分为 prefix/name（ustar 长文件名）。
// 若总长 <= 100，全部放 name；否则在最后一个 '/' 处拆分，prefix<=155、name<=100。
void splitPath(const std::string& utf8Path, std::string& prefix, std::string& name) {
    if (utf8Path.size() <= kNameLen) {
        name = utf8Path;
        prefix.clear();
        return;
    }
    // 从能让 name <= 100 的位置向前找 '/'
    for (size_t cut = std::min(utf8Path.size() - 1, kPrefixLen + 1); cut > 0; --cut) {
        if (utf8Path[cut] == '/' && cut <= kPrefixLen &&
            utf8Path.size() - cut - 1 <= kNameLen) {
            prefix = utf8Path.substr(0, cut);
            name = utf8Path.substr(cut + 1);
            return;
        }
    }
    // 无法拆分，硬截断（极少发生）
    name = utf8Path.substr(0, kNameLen);
    prefix.clear();
}

// 递归收集目录树（含文件、目录、符号链接），并读取文件 ID 与符号链接目标。
struct Node {
    std::wstring rel;       // 归档内相对路径（正斜杠）
    std::wstring abs;       // 实际绝对路径
    FileType type;
    uint64_t size;
    uint64_t mtime;
    uint32_t volumeSerial;
    uint64_t fileIndex;
    std::wstring symTarget; // 符号链接目标
};

std::wstring toPosix(const std::wstring& s) {
    std::wstring r = s;
    std::replace(r.begin(), r.end(), L'\\', L'/');
    return r;
}

bool collectNodes(const std::wstring& root, const std::wstring& relDir,
                  std::vector<Node>& nodes, std::string* err) {
    const std::wstring absDir = relDir.empty() ? root : root + L"\\" + relDir;
    std::vector<std::pair<std::wstring, FileType>> entries;
    if (!FileSystem::listDirectory(absDir, entries)) {
        if (err) *err = "无法枚举目录: " + wideToUtf8(absDir);
        return false;
    }
    for (const auto& entry : entries) {
        const std::wstring rel = relDir.empty() ? entry.first : relDir + L"\\" + entry.first;
        const std::wstring abs = root + L"\\" + rel;
        FileInfo info;
        if (!FileSystem::getFileInfo(abs, info)) {
            if (err) *err = "无法获取文件信息: " + wideToUtf8(abs);
            return false;
        }
        Node n;
        n.rel = toPosix(rel);
        n.abs = abs;
        n.type = info.type;
        n.size = info.size;
        n.mtime = info.modifiedTime;
        n.volumeSerial = info.volumeSerial;
        n.fileIndex = info.fileIndex;
        if (info.type == FileType::Symlink) {
            FileSystem::readSymlinkTarget(abs, n.symTarget);
        }
        nodes.push_back(n);
        // 目录：递归（符号链接目录不递归，避免成环）
        if (info.type == FileType::Directory) {
            if (!collectNodes(root, rel, nodes, err)) return false;
        }
    }
    return true;
}

// 读取一个 512 字节块；返回 0=EOF/全零块，1=正常块，-1=错误。
int readBlock(std::istream& in, char* block) {
    in.read(block, kBlock);
    const std::streamsize got = in.gcount();
    if (got == 0) return 0;
    if (got != static_cast<std::streamsize>(kBlock)) return -1;
    for (size_t i = 0; i < kBlock; ++i) {
        if (block[i] != 0) return 1;
    }
    return 0;
}

// 解析八进制字段（容忍 NUL/空格结尾）。
uint64_t parseOctal(const char* field, size_t len) {
    uint64_t v = 0;
    for (size_t i = 0; i < len; ++i) {
        const char c = field[i];
        if (c == '\0' || c == ' ') break;
        if (c < '0' || c > '7') break;
        v = v * 8 + static_cast<uint64_t>(c - '0');
    }
    return v;
}

}  // namespace

bool TarPacker::packDirectory(const std::wstring& srcDir,
                              const std::wstring& tarFile,
                              std::string* err,
                              const TarProgressCallback& progress) {
    std::vector<Node> nodes;
    if (!collectNodes(srcDir, L"", nodes, err)) return false;

    // 硬链接表：(卷, 文件索引) -> 首个归档路径。
    std::map<std::pair<uint32_t, uint64_t>, std::wstring> firstSeen;

    std::ofstream out(tarFile.c_str(), std::ios::binary | std::ios::trunc);
    if (!out) {
        if (err) *err = "无法创建 TAR 文件: " + wideToUtf8(tarFile);
        return false;
    }

    char block[kBlock];

    for (const Node& n : nodes) {
        if (progress) progress(n.rel);

        TarHeader h;
        std::memset(&h, 0, sizeof(h));
        std::memcpy(h.magic, "ustar", 5);  // ustar\0
        h.version[0] = '0';
        h.version[1] = '0';

        std::string utf8Rel = wideToUtf8(n.rel);
        std::string prefix, name;
        splitPath(utf8Rel, prefix, name);
        writeString(h.name, sizeof(h.name), name);
        writeString(h.prefix, sizeof(h.prefix), prefix);

        // 默认权限：目录 755，文件 644
        const char* mode = (n.type == FileType::Directory) ? "755" : "644";
        writeOctal(h.mode, sizeof(h.mode), std::strtoul(mode, nullptr, 8));
        writeOctal(h.uid, sizeof(h.uid), 0);
        writeOctal(h.gid, sizeof(h.gid), 0);
        writeOctal(h.mtime, sizeof(h.mtime), n.mtime);

        if (n.type == FileType::Symlink) {
            // 符号链接：typeflag='2'，linkname=目标，size=0
            h.typeflag = kTarSymlink;
            writeString(h.linkname, sizeof(h.linkname), wideToUtf8(n.symTarget));
            writeOctal(h.size, sizeof(h.size), 0);
            fillChecksum(h);
            std::memcpy(block, &h, kBlock);
            out.write(block, kBlock);
        } else if (n.type == FileType::Directory) {
            // 目录：typeflag='5'
            h.typeflag = kTarDirectory;
            writeOctal(h.size, sizeof(h.size), 0);
            fillChecksum(h);
            std::memcpy(block, &h, kBlock);
            out.write(block, kBlock);
        } else {
            // 普通文件：先判断是否为硬链接组的非首个条目
            auto key = std::make_pair(n.volumeSerial, n.fileIndex);
            auto it = firstSeen.find(key);
            if (it != firstSeen.end()) {
                // 硬链接：typeflag='1'，linkname=首个归档路径，size=0
                h.typeflag = kTarHardLink;
                writeString(h.linkname, sizeof(h.linkname), wideToUtf8(it->second));
                writeOctal(h.size, sizeof(h.size), 0);
                fillChecksum(h);
                std::memcpy(block, &h, kBlock);
                out.write(block, kBlock);
            } else {
                // 硬链接组首个：普通文件 typeflag='0'，写入实际内容
                firstSeen[key] = n.rel;
                h.typeflag = kTarRegular;
                writeOctal(h.size, sizeof(h.size), n.size);
                fillChecksum(h);
                std::memcpy(block, &h, kBlock);
                out.write(block, kBlock);

                // 写入文件内容，分块拷贝
                std::ifstream in(n.abs.c_str(), std::ios::binary);
                if (!in) {
                    if (err) *err = "无法打开文件: " + wideToUtf8(n.abs);
                    return false;
                }
                uint64_t remaining = n.size;
                while (remaining > 0) {
                    std::memset(block, 0, kBlock);
                    const size_t want = static_cast<size_t>(std::min<uint64_t>(remaining, kBlock));
                    in.read(block, want);
                    if (static_cast<size_t>(in.gcount()) != want) {
                        if (err) *err = "读取文件内容失败: " + wideToUtf8(n.abs);
                        return false;
                    }
                    out.write(block, kBlock);
                    remaining -= want;
                }
            }
        }
        if (!out) {
            if (err) *err = "写入 TAR 失败";
            return false;
        }
    }

    // 结束：两个全零块
    std::memset(block, 0, kBlock);
    out.write(block, kBlock);
    out.write(block, kBlock);
    out.close();
    return static_cast<bool>(out) || out.eof();
}

bool TarPacker::extractTo(const std::wstring& tarFile,
                          const std::wstring& destDir,
                          std::string* err,
                          const TarProgressCallback& progress) {
    std::ifstream in(tarFile.c_str(), std::ios::binary);
    if (!in) {
        if (err) *err = "无法打开 TAR 文件: " + wideToUtf8(tarFile);
        return false;
    }
    if (!FileSystem::createDirectories(destDir)) {
        if (err) *err = "无法创建目标目录: " + wideToUtf8(destDir);
        return false;
    }

    char block[kBlock];
    while (true) {
        const int r = readBlock(in, block);
        if (r == 0) break;
        if (r < 0) {
            if (err) *err = "TAR 块读取失败（文件可能损坏）";
            return false;
        }

        const auto* h = reinterpret_cast<const TarHeader*>(block);
        // 合并 prefix + name
        std::string prefix(h->prefix, strnlen(h->prefix, sizeof(h->prefix)));
        std::string name(h->name, strnlen(h->name, sizeof(h->name)));
        std::string utf8Rel = prefix.empty() ? name : (prefix + "/" + name);
        std::wstring rel = utf8ToWide(utf8Rel);
        if (progress) progress(rel);

        const uint64_t size = parseOctal(h->size, sizeof(h->size));
        const char type = (h->typeflag == '\0') ? kTarRegular : h->typeflag;
        std::string link(h->linkname, strnlen(h->linkname, sizeof(h->linkname)));
        std::wstring linkW = utf8ToWide(link);

        // 归档内路径统一为 Windows 反斜杠
        std::wstring winRel = rel;
        std::replace(winRel.begin(), winRel.end(), L'/', L'\\');
        const std::wstring outPath = destDir + L"\\" + winRel;

        // 确保父目录存在（条目在归档根下时无父目录，跳过）
        const auto ensureParent = [&](const std::wstring& p) -> bool {
            const size_t sep = p.find_last_of(L"\\/");
            if (sep == std::wstring::npos || sep == 0) return true;
            return FileSystem::createDirectories(p.substr(0, sep));
        };

        if (type == kTarDirectory) {
            if (!FileSystem::createDirectories(outPath)) {
                if (err) *err = "无法创建目录: " + wideToUtf8(outPath);
                return false;
            }
        } else if (type == kTarSymlink) {
            // 符号链接：根据目标是否带斜杠/是否存在判断目录或文件
            bool targetIsDir = (!link.empty() && (link.back() == '/' || link.back() == '\\'));
            if (!ensureParent(outPath)) {
                if (err) *err = "无法创建符号链接父目录";
                return false;
            }
            if (!FileSystem::createSymlink(outPath, linkW, targetIsDir, err)) {
                if (err) *err = "创建符号链接失败（可能需要开发者模式/管理员）: " + wideToUtf8(outPath);
                return false;
            }
        } else if (type == kTarHardLink) {
            // 硬链接：linkW 是归档内首个文件路径，转为 Windows 路径
            std::wstring linkWin = linkW;
            std::replace(linkWin.begin(), linkWin.end(), L'/', L'\\');
            const std::wstring target = destDir + L"\\" + linkWin;
            if (!ensureParent(outPath)) {
                if (err) *err = "无法创建硬链接父目录";
                return false;
            }
            if (!FileSystem::createHardLink(outPath, target, err)) {
                if (err) *err = "创建硬链接失败: " + wideToUtf8(outPath);
                return false;
            }
        } else {
            // 普通文件：写出 size 字节
            if (!ensureParent(outPath)) {
                if (err) *err = "无法创建文件父目录";
                return false;
            }
            std::ofstream out(outPath.c_str(), std::ios::binary | std::ios::trunc);
            if (!out) {
                if (err) *err = "无法创建文件: " + wideToUtf8(outPath);
                return false;
            }
            uint64_t remaining = size;
            while (remaining > 0) {
                const int dr = readBlock(in, block);
                if (dr == 0) {
                    if (err) *err = "TAR 数据意外结束";
                    return false;
                }
                const size_t want = static_cast<size_t>(std::min<uint64_t>(remaining, kBlock));
                out.write(block, want);
                remaining -= want;
            }
            if (!out) {
                if (err) *err = "写入文件失败: " + wideToUtf8(outPath);
                return false;
            }
        }
    }
    return true;
}

bool TarPacker::listEntries(const std::wstring& tarFile,
                            std::vector<std::wstring>& outPaths,
                            std::string* err) {
    outPaths.clear();
    std::ifstream in(tarFile.c_str(), std::ios::binary);
    if (!in) {
        if (err) *err = "无法打开 TAR 文件: " + wideToUtf8(tarFile);
        return false;
    }
    char block[kBlock];
    while (true) {
        const int r = readBlock(in, block);
        if (r == 0) break;
        if (r < 0) {
            if (err) *err = "TAR 块读取失败";
            return false;
        }
        const auto* h = reinterpret_cast<const TarHeader*>(block);
        std::string prefix(h->prefix, strnlen(h->prefix, sizeof(h->prefix)));
        std::string name(h->name, strnlen(h->name, sizeof(h->name)));
        std::string utf8Rel = prefix.empty() ? name : (prefix + "/" + name);
        outPaths.push_back(utf8ToWide(utf8Rel));

        // 跳过数据块
        const uint64_t size = parseOctal(h->size, sizeof(h->size));
        const uint64_t dataBlocks = (size + kBlock - 1) / kBlock;
        in.seekg(static_cast<std::streamoff>(dataBlocks * kBlock), std::ios::cur);
        if (!in) {
            if (err) *err = "TAR 数据跳转失败";
            return false;
        }
    }
    return true;
}

}  // namespace backup
