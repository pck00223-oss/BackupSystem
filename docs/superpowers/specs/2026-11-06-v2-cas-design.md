# BackupSystem v2（v2-cas）设计规格

- 分支：`v2-cas`（基于 `main@37f04c0`，v1 代码保留）
- 状态：设计已确认，待实现
- 目标形态：C++17 / Windows / CLI / 内容定义分块 + 内容寻址存储 + 快照
- 关键约束：**不使用继承，不定义基类或虚函数；通过明确的具体类和组合组织代码**

---

## 1. 目标与范围

### 1.1 第一阶段必须完成

- 全量 / 增量备份；
- FastCDC 内容定义分块；
- 块级去重（内容寻址存储，CAS）；
- 快照（文件 → 块引用列表）与时间点恢复；
- 完整性校验（块级、文件级、仓库级）；
- AES-256-CBC 与 3DES-CBC 两套加解密算法；
- CLI；
- GTest、测试脚本、GitHub Actions CI。

### 1.2 第一阶段不做

- GUI 页面（`MainWindow` / `TaskStore` 不进入 v2）；
- TAR 打包（与 CAS 仓库形态冲突）；
- 常驻调度器 / `schtasks` 集成（用操作系统计划任务直接调用 CLI）；
- 压缩（zstd）、CDP、目录 Merkle 树；
- v1 仓库格式兼容与迁移（v2 使用独立仓库格式）。

---

## 2. 仓库布局与对象格式

```text
<repo>/
├── objects/
│   └── ab/cd/<plaintext-sha256>.obj
├── manifests/
│   └── <snapshot-id>.manifest
├── refs/
│   └── latest
├── logs/
│   └── backup.log
└── .lock
```

### 2.1 Chunk 对象

- `chunk_id = SHA-256(plaintext_chunk)`，同时作为对象地址与去重 key。
- 对象文件路径：`objects/<id[0:2]>/<id[2:4]>/<id>.obj`。
- 对象格式（小端整数）：

```text
magic      4 bytes   "BSO2"
version    1 byte    1
algorithm  1 byte    0=none, 1=aes256, 2=3des
reserved   2 bytes
plain_len  8 bytes
cipher_len 8 bytes
iv_len     1 byte
iv         iv_len bytes
ciphertext cipher_len bytes
```

- 未加密时 `iv_len=0`、`cipher_len=plain_len`。
- 加密时每个新对象使用 `BCryptGenRandom` 生成随机 IV；相同明文块只写入一次，后续直接复用对象。

### 2.2 Object 写入

- 先写同目录临时文件；
- 成功后 `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING)` 原子替换；
- 已存在 object 不重写、不重新加密。

---

## 3. Manifest v2

UTF-8 文本，路径采用 percent-encoding，避免分隔符和换行歧义。
编码规则：对路径 UTF-8 字节中除 `A-Z a-z 0-9 - _ . /` 之外的字节写成 `%HH`（大写十六进制），
`=`、制表符、换行、`%` 必须转义。

```text
[snapshot]
version=2
snapshot_id=20261106-120000
created=2026-11-06 12:00:00
source=<encoded-source-path>
crypto=none|aes256|3des
password_verifier=<sha256 or empty>
file_count=3
chunk_min=2048
chunk_avg=8192
chunk_max=65536

[file]
path=<encoded-relative-path>
type=file
size=12345
mtime_100ns=133...
attributes=0x20
file_hash=<sha256-of-plaintext-file>
chunk_count=2

[chunk]
id=<plaintext-sha256>
offset=0
plain_len=8192
object_size=8224
object_hash=<sha256-of-object-file>

[chunk]
id=<plaintext-sha256>
offset=8192
plain_len=4153
object_size=4176
object_hash=<sha256-of-object-file>

[file]
path=<encoded-empty-file>
type=file
size=0
mtime_100ns=133...
attributes=0x20
file_hash=e3b0c442...
chunk_count=0

[file]
path=<encoded-empty-dir>
type=dir
attributes=0x10
```

规则：

- `[chunk]` 紧跟所属 `[file]`，按 `offset` 升序；
- 文件重建：按 offset 拼接块，校验每块 `plain_len`，最终校验 `size` 和 `file_hash`；
- 空文件：`size=0, chunk_count=0`，`file_hash` 为 SHA-256 空串；
- 空目录：独立 `type=dir` 条目；
- 文件元数据：mtime（100ns）、attributes（只读/隐藏）、整文件 hash；
- **特殊文件**：reparse point / junction / 符号链接一律跳过并写入 warning；不递归；
- 单个文件读失败或复制失败：记录错误并继续，整体结果标记为失败，但已写对象可被后续 GC 处理。

---

## 4. 备份流水线

```text
FileScanner
  -> 文件级快速路径（size + mtime_100ns + attributes）
      -> 未变化：复用上一快照的 chunk 引用
      -> 已变化：FastCDC 分块
          -> 计算 chunk_id = SHA-256(plaintext)
          -> ChunkStore 已存在？复用
          -> 不存在：加密 -> 写 object -> 记录 cipher/object hash
  -> 写 chunk 引用到新 Manifest
  -> 原子提交 Manifest / latest
```

### 4.1 文件级快速路径

- 上一快照存在同路径文件且 `size + mtime_100ns + attributes` 完全一致：直接复用块列表；
- 否则读取文件并重新分块；
- `--rescan` 强制忽略快速路径；
- 不实现“复用旧块的前缀/滚动匹配”优化。

### 4.2 FastCDC 参数

- 默认 min / avg / max：2 KiB / 8 KiB / 64 KiB；
- 使用 gear-based rolling hash；
- 分块必须确定性：相同输入、相同参数 → 相同边界；
- 参数写入 Manifest 头部（`chunk_min` / `chunk_avg` / `chunk_max`），恢复只依赖 Manifest，不重新分块。

---

## 5. 加密与去重

### 5.1 顺序（强制）

1. 分块；
2. 明文块 SHA-256 → `chunk_id`；
3. 去重检查；
4. 仅新块加密并写 object；
5. Manifest 记录明文块哈希与密文对象哈希。

不得先加密再哈希去重。

### 5.2 仓库级密码约束

- Manifest 头部记录 `crypto` 与 `password_verifier`；
- `password_verifier = SHA-256("BackupSystem-v2|" + password)`；
- 读 object 前先校验 verifier；
- 增量备份要求算法与 verifier 与上一份 Manifest 一致，否则拒绝并提示全量重建；
- 加密仓库与明文仓库不得混用。

### 5.3 算法实现（无继承）

- `Aes256Cipher`：独立具体类，AES-CBC，32 字节 key，16 字节块；
- `TripleDesCipher`：独立具体类，3DES-CBC，24 字节 key，8 字节块；
- 两者不共享基类、不使用虚函数；
- `CryptoService` 通过 `enum class CryptoAlgorithm { None, Aes256, TripleDes }` 与 `std::variant<Aes256Cipher, TripleDesCipher>` 选择实现；
- BCrypt 的打开算法、导入 key、加解密步骤抽成共享 free function，供两个具体类组合调用。

---

## 6. 一致性、GC 与快照

### 6.1 提交顺序

1. 写全部 object（临时文件 + 原子 rename）；
2. 写新 Manifest（临时文件 + 原子 rename）；
3. 原子更新 `refs/latest`；
4. 按 `--keep N` 清理旧快照。

崩溃语义：

- object 写完但 Manifest 未提交：只留下 orphan object，旧快照不受影响；
- Manifest 写完但 latest 未更新：快照可通过 `--snapshot <id>` 使用，latest 指向旧快照；
- 不实现 journal / 自动回滚；orphan 由 GC 清理。

### 6.2 GC（mark-sweep）

- `gc` 遍历 `manifests/*.manifest`，标记全部可达 `chunk_id`；
- 删除 `objects/` 下未标记对象；
- 全程持有 `FileLock`；
- `gc --dry-run` 只统计待删除对象数与字节数；
- 不维护持久化 refcount。

### 6.3 快照

- `snapshot_id = yyyyMMdd-HHmmss`，同秒冲突追加 `-1`、`-2`；
- `manifests/<snapshot_id>.manifest` 为唯一真实来源；
- `refs/latest` 为便捷指针；
- `--keep N` 在提交成功后清理最旧的 N 份之外的快照；
- 恢复默认 latest，也支持 `--snapshot <id>`。

---

## 7. 校验

### 7.1 三层校验

1. **块级**
   - object 存在；
   - 文件大小与 `object_size` 一致；
   - 文件 SHA-256 与 `object_hash` 一致；
   - 若有密码：解密后 `SHA-256(plaintext) == chunk_id`。
2. **文件级**
   - 按 offset 重组；
   - 总长度等于 `size`；
   - SHA-256 等于 `file_hash`。
3. **仓库级**
   - 所有 Manifest 引用的 chunk_id 均存在对应 object。

加密仓库无密码时只做密文层校验，CLI 标记为 `partial`，不得报告完整通过。

### 7.2 统计输出

备份完成后输出：

- 新增 / 修改 / 删除 / 未变化文件数；
- 逻辑总字节；
- 新增块数 / 复用块数；
- 唯一存储字节；
- 去重率 = `1 - 唯一存储字节 / 逻辑总字节`；
- 节省空间。

---

## 8. 具体类与职责（全部为组合，无继承）

```text
AppConfig
  值对象：repo/source/crypto/password/keep 等

CliParser
  解析 argv，生成 AppConfig / 命令枚举

FileScanner
  scan(root) -> ScanResult { vector<FileEntry>, vector<Warning>, vector<Error> }

FastCdcChunker
  split(buffer) -> vector<ChunkSpan>（测试用）
  splitFile(path, callback) -> bool（生产用，流式回调）

ChunkStore
  has(id)
  put(id, plaintext, crypto) -> ChunkObjectInfo
  read(id, password) -> plaintext / error
  listObjects()
  deleteObjects(ids)

CryptoService
  seal(algorithm, password, plaintext) -> object bytes
  open(algorithm, password, object bytes) -> plaintext / error
  makeVerifier(password)

ManifestStore
  save(snapshot)
  load(snapshotId)
  list()
  loadLatest()
  updateLatest(snapshotId)

SnapshotService
  create(manifest)
  list()
  prune(keep)
  select(id)

BackupService
  run(AppConfig) -> BackupStats

RestoreService
  run(snapshotId, target, overwrite) -> RestoreStats

VerifyService
  verifyChunk / verifyFile / verifyRepo

FileLock
  acquire/release

Logger
  info/warn/error
```

约束：

- 全部是具体类；
- 不使用 `: public` 继承；
- 不使用 `virtual`；
- 依赖通过构造函数传入引用或值对象；
- 任何类不得直接负责其他层的职责。

---

## 9. CLI（第一阶段）

```text
backupv2 backup  --repo <dir> --source <dir>
                 [--crypto none|aes256|3des --password <pwd>]
                 [--keep N] [--rescan]
backupv2 restore --repo <dir> --to <dir>
                 [--snapshot <id>] [--overwrite]
backupv2 verify  --repo <dir> [--snapshot <id>|--all]
                 [--password <pwd>]
backupv2 snapshot list --repo <dir>
backupv2 snapshot prune --repo <dir> --keep N
backupv2 gc --repo <dir> [--dry-run]
```

可执行文件：`backupv2.exe`，与 v1 的 `backupapp.exe` 并存。

---

## 10. 测试与脚本

### 10.1 GTest

- FastCDC：确定性、最小/最大块、边界输入、重复数据；
- ChunkStore：相同内容只产生一个 object、不同内容分离、损坏对象识别；
- 加密：AES-256 / 3DES 往返、错误密码、篡改密文、加密 + 去重；
- Manifest：往返、空文件、空目录、percent-encoded 路径、块引用；
- Snapshot：创建、列表、latest、指定快照恢复、prune；
- Backup/Restore：全量、增量、修改、删除、插入数据后的高去重率、恢复 hash；
- Verify：块级/文件级/仓库级、缺块、坏块、partial（无密码）；
- GC：只删除不可达对象、dry-run 不删除；
- 环境隔离：TempDir / EnvGuard / LoggerGuard。

### 10.2 脚本

- `scripts/run_checks.ps1`：strict 构建 + ctest + cppcheck + hardening；
- `scripts/test-backup-demo.ps1`：适配 v2 仓库布局，生成两版数据、展示去重统计、篡改对象、verify、restore、GC。

### 10.3 CI

- Windows + MSYS2 UCRT64；
- strict 构建 + ctest + cppcheck；
- 两套加密算法测试都必须在 CI 中运行。

---

## 11. 验收标准

1. 同一份数据连续两次备份，第二次新增块为 0，去重率 100%；
2. 在大文件中插入少量字节，后续块仍大量复用，CLI 打印可量化的去重率；
3. 分别用 AES-256 / 3DES 完成备份、verify、恢复，内容与源一致；
4. 篡改任一 object 后 verify 失败，恢复不产生错误内容；
5. 写 Manifest 前中断：旧 latest 仍可恢复；GC 能清理 orphan；
6. 空文件、空目录、中文路径可完整备份与恢复；
7. `ctest` 全绿、strict 构建 0 警告、cppcheck 0 issue。

---

## 12. 明确不做的事

- 不做 GUI 页面；
- 不做 TAR；
- 不做常驻调度器 / schtasks XML；
- 不做压缩/CDP/目录 Merkle 树；
- 不做 v1 仓库格式兼容；
- 不做复杂崩溃回滚 journal；
- 不做多线程分块/并行上传；
- 不使用继承、基类、虚函数。
