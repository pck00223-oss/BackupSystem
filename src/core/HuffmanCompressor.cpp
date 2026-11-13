// HuffmanCompressor.cpp - 手写哈夫曼压缩/解压实现（无损）
#include "core/HuffmanCompressor.h"

#include <algorithm>
#include <cstring>
#include <queue>
#include <string>

namespace backup {

namespace {

// ---- 小端读写辅助 ----
void appendU16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}
void appendU32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}
void appendU64(std::vector<uint8_t>& out, uint64_t v) {
    for (int i = 0; i < 8; ++i)
        out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
}
uint16_t readU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
uint32_t readU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
uint64_t readU64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v |= static_cast<uint64_t>(p[i]) << (8 * i);
    return v;
}

// ---- 哈夫曼树节点（用索引，避免裸指针）----
struct Node {
    uint32_t freq = 0;
    int symbol = -1;  // 叶子: 0..255，内部: -1
    int left = -1;
    int right = -1;
};

// 最小堆比较：频率小的优先；频率相同则创建索引小的优先（保证编解码建树确定一致）。
struct NodeCmp {
    const std::vector<Node>* nodes;
    bool operator()(int a, int b) const {
        const Node& x = (*nodes)[a];
        const Node& y = (*nodes)[b];
        if (x.freq != y.freq) return x.freq > y.freq;
        return a > b;
    }
};

// 由词频构建哈夫曼树，返回根索引（空词频返回 -1，单符号返回该叶子索引）。
int buildTree(const uint32_t freq[256], std::vector<Node>& nodes) {
    nodes.clear();
    std::priority_queue<int, std::vector<int>, NodeCmp> pq(NodeCmp{&nodes});
    for (int s = 0; s < 256; ++s) {
        if (freq[s] == 0) continue;
        nodes.push_back(Node{freq[s], s, -1, -1});
        pq.push(static_cast<int>(nodes.size()) - 1);
    }
    if (nodes.empty()) return -1;
    if (nodes.size() == 1) return 0;
    while (pq.size() > 1) {
        const int a = pq.top();
        pq.pop();
        const int b = pq.top();
        pq.pop();
        nodes.push_back(Node{nodes[a].freq + nodes[b].freq, -1, a, b});
        pq.push(static_cast<int>(nodes.size()) - 1);
    }
    return pq.top();
}

// 递归生成各叶子的前缀码（左 0 右 1）。
// prefix 复用同一字符串，通过 push/pop 维护当前路径，避免每层拷贝。
void generateCodes(const std::vector<Node>& nodes, int idx,
                   std::string& prefix, std::vector<std::string>& codes) {
    const Node& n = nodes[idx];
    if (n.symbol >= 0) {
        codes[n.symbol] = prefix.empty() ? "0" : prefix;
        return;
    }
    prefix.push_back('0');
    generateCodes(nodes, n.left, prefix, codes);
    prefix.pop_back();
    prefix.push_back('1');
    generateCodes(nodes, n.right, prefix, codes);
    prefix.pop_back();
}

// 比特打包器：从字节最高位开始填充。
struct BitWriter {
    std::vector<uint8_t> bytes;
    uint8_t cur = 0;
    int bits = 0;

    void writeBit(int b) {
        cur = static_cast<uint8_t>((cur << 1) | (b & 1));
        if (++bits == 8) {
            bytes.push_back(cur);
            cur = 0;
            bits = 0;
        }
    }
    // 返回最后一个字节的有效位数（恰好对齐时为 8）。
    int flush() {
        const int valid = bits;
        if (bits > 0) {
            bytes.push_back(static_cast<uint8_t>(cur << (8 - bits)));
            cur = 0;
            bits = 0;
        }
        return valid == 0 ? 8 : valid;
    }
};

// 比特读取器。
struct BitReader {
    const uint8_t* p = nullptr;
    size_t n = 0;
    size_t bytePos = 0;
    int bitPos = 0;

    bool nextBit(int& b) {
        if (bytePos >= n) return false;
        b = (p[bytePos] >> (7 - bitPos)) & 1;
        if (++bitPos == 8) {
            bitPos = 0;
            ++bytePos;
        }
        return true;
    }
};

}  // namespace

std::vector<uint8_t> HuffmanCompressor::compress(const uint8_t* data, size_t len) const {
    uint32_t freq[256] = {0};
    for (size_t i = 0; i < len; ++i) ++freq[data[i]];

    std::vector<Node> nodes;
    const int root = buildTree(freq, nodes);

    std::vector<std::string> codes(256);
    if (root >= 0) {
        std::string prefix;
        generateCodes(nodes, root, prefix, codes);
    }

    BitWriter bw;
    for (size_t i = 0; i < len; ++i)
        for (char c : codes[data[i]]) bw.writeBit(c - '0');
    const int validBits = bw.flush();

    std::vector<uint8_t> out;
    appendU64(out, static_cast<uint64_t>(len));
    uint16_t symCount = 0;
    for (int s = 0; s < 256; ++s)
        if (freq[s] > 0) ++symCount;
    appendU16(out, symCount);
    for (int s = 0; s < 256; ++s) {
        if (freq[s] == 0) continue;
        out.push_back(static_cast<uint8_t>(s));
        appendU32(out, freq[s]);
    }
    out.push_back(static_cast<uint8_t>(validBits));
    out.insert(out.end(), bw.bytes.begin(), bw.bytes.end());
    return out;
}

std::vector<uint8_t> HuffmanCompressor::decompress(const uint8_t* data, size_t len,
                                                    bool* success) const {
    if (success) *success = false;

    // 最小头部：8(origLen) + 2(symCount)，空数据时到此为止。
    if (!data || len < 10) return {};
    size_t pos = 0;
    const uint64_t originalLen = readU64(data + pos);
    pos += 8;
    const uint16_t symCount = readU16(data + pos);
    pos += 2;

    uint32_t freq[256] = {0};
    for (uint16_t i = 0; i < symCount; ++i) {
        if (pos + 5 > len) return {};  // 词频表截断
        const uint8_t symbol = data[pos++];
        const uint32_t f = readU32(data + pos);
        pos += 4;
        if (freq[symbol] != 0) return {};  // 重复符号，数据非法
        freq[symbol] = f;
    }

    if (pos + 1 > len) return {};  // 缺 validBits
    const int validBits = data[pos++];
    if (validBits < 1 || validBits > 8) return {};

    const uint8_t* stream = data + pos;
    const size_t streamLen = len - pos;

    // 空数据：词频表与比特流都应为空。
    if (originalLen == 0) {
        if (symCount != 0 || streamLen != 0) return {};
        if (success) *success = true;
        return {};
    }

    std::vector<Node> nodes;
    const int root = buildTree(freq, nodes);
    if (root < 0) return {};

    // 校验比特流长度与声明的有效位数是否自洽。
    const uint64_t totalBits = (streamLen == 0 ? 0
            : static_cast<uint64_t>(streamLen - 1) * 8 + validBits);
    if (streamLen == 0 || totalBits < originalLen) return {};

    std::vector<uint8_t> out;
    out.reserve(static_cast<size_t>(originalLen));

    BitReader br{stream, streamLen, 0, 0};

    // 单符号特例：每个输入字节对应编码 "0"，无需走树。
    if (nodes.size() == 1) {
        const int sym = nodes[0].symbol;
        for (uint64_t i = 0; i < originalLen; ++i) {
            int b;
            if (!br.nextBit(b)) return {};
            out.push_back(static_cast<uint8_t>(sym));
        }
        if (success) *success = true;
        return out;
    }

    int cur = root;
    for (uint64_t i = 0; i < originalLen; ++i) {
        while (nodes[cur].symbol < 0) {
            int b;
            if (!br.nextBit(b)) return {};
            cur = (b == 0) ? nodes[cur].left : nodes[cur].right;
        }
        out.push_back(static_cast<uint8_t>(nodes[cur].symbol));
        cur = root;
    }

    if (out.size() != originalLen) return {};
    if (success) *success = true;
    return out;
}

}  // namespace backup
