# BackupSystem - 基于 C++ / Windows 的数据备份软件

面向软件工程 / 计算机复试的本地数据备份工具。采用**分层架构 + 面向对象设计**，实现
全量备份、增量备份、数据恢复、完整性校验、Manifest 增量检测、SHA-256 校验、任务调度、日志与异常处理。

## 功能对照（需求文档 → 实现状态）

| 功能 | 状态 | 说明 |
|------|------|------|
| 全量备份 | ✅ 已完成 | 首次备份全部文件（含空目录） |
| 增量备份 | ✅ 已完成 | 元数据快速判断 + Hash 二次确认（两级变更检测，100ns 精度时间） |
| 数据恢复 | ✅ 已完成 | 恢复后 Hash 校验 + 元数据（修改时间），冲突跳过记为警告 |
| 完整性校验 | ✅ 已完成 | `verify` 子命令：按 Manifest 校验 data/ 仓库，报告缺失/损坏 |
| Manifest | ✅ 已完成 | 文本格式，可解释，UTF-8，原子写入，头部/数值/条目数校验 |
| Hash 校验 | ✅ 已完成 | 自实现 SHA-256，无第三方依赖 |
| 自定义筛选 | ✅ 已完成 | 扩展名 / 路径（目录段匹配）/ 大小 / 修改时间 / 空文件 |
| 任务调度 | ✅ 已完成 | 每天 HH:MM 定时，错过自动补跑，stop 后可重新 start |
| 备份历史 | ✅ 已完成 | history.log 追加记录 |
| 日志 | ✅ 已完成 | INFO / WARNING / ERROR，文件 + 控制台 |
| 异常处理 | ✅ 已完成 | 单个文件失败不中断，汇总统计，失败/取消不覆盖旧 Manifest |
| 任务取消 | ✅ 已完成 | 扫描/复制阶段均可取消，取消后保留旧清单 |
| 空目录保留 | ✅ 已完成 | 目录条目进入 Manifest，恢复时重建（目录不受筛选规则限制） |
| 符号链接处理 | ✅ 已完成 | 默认跳过 junction/符号链接（防递归成环），记录为警告 |
| 文件↔目录互换 | ✅ 已完成 | 同路径类型变化按"删除旧条目 + 新增"处理，同步清理旧数据 |
| 长路径支持 | ✅ 已完成 | 超过 240 字符自动加 `\\?\` 前缀 |
| CI/CD | ✅ 已完成 | GitHub Actions：strict 构建 + 全部测试 + cppcheck |
| 测试 | ✅ 已完成 | 单元 + 集成测试（不依赖第三方框架） |
| 打包 / 压缩 / AES | ⏳ 扩展预留 | 对应文档 Phase 5/7/8，接口已预留 |
| GUI | ⏳ 扩展预留 | 对应文档 Phase 9 |

## 架构（分层）

```
CLI / GUI (应用层)
      ↓
Application 层  BackupTask / TaskScheduler / ConfigLoader
      ↓
Business 层     BackupManager / RestoreManager / VerifyManager / Manifest / FileComparator / FileFilter
      ↓
File Engine 层  FileScanner / FileCopier / FileSystem
      ↓
Windows API     CreateFileW / FindFirstFileW / ReadFile / WriteFile / MoveFileExW ...
```

## 目录结构

```
BackupSystem/
├── .github/workflows/ci.yml   GitHub Actions CI（strict 构建 + 测试 + cppcheck）
├── CMakeLists.txt
├── README.md
├── 数据备份软件设计与实现方案.md   ← 需求文档
├── include/   头文件（按层分包：core / engine / business / app）
├── src/       实现文件（main.cpp 为命令行入口）
├── tests/     单元与集成测试
├── config/    default.conf 默认配置
├── scripts/   run_checks.ps1 一键健壮性检查脚本
└── docs/
```

## 构建（Windows，需要 CMake + 编译器）

支持 MinGW-w64（推荐，含 Ninja）与 MSVC。

```bash
# MinGW-w64 + Ninja
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# MSVC (Visual Studio)
cmake -S . -B build-msvc -G "Visual Studio 17 2022"
cmake --build build-msvc --config Release
```

构建产物：
- `build/backupapp.exe`   命令行主程序
- `build/backup_tests.exe` 测试程序

### 健壮性检查构建选项

```bash
# 严格编译警告（-Wconversion -Wshadow -Wsign-conversion 等）
cmake -S . -B build-strict -G Ninja -DSTRICT_WARNINGS=ON

# 运行时加固（GLibC++ 调试模式 + 栈保护 + 整数溢出 trap）
cmake -S . -B build-hard -G Ninja -DHARDENING=ON
```

## 运行

```bash
# 全量备份：把 D:\MyData 备份到 D:\Backup
build\backupapp.exe backup --source D:\MyData --target D:\Backup --type full

# 增量备份（第二次执行只处理变化文件）
build\backupapp.exe backup --source D:\MyData --target D:\Backup --type incremental

# 自定义筛选：只备份 .cpp/.h，最近 30 天修改，大于 1KB
build\backupapp.exe backup --source D:\MyData --target D:\Backup --include-ext .cpp,.h

# 恢复：把备份恢复到 E:\Restored
build\backupapp.exe restore --backup D:\Backup --to E:\Restored

# 完整性校验：按 Manifest 检查 data/ 仓库是否有缺失或损坏
build\backupapp.exe verify --backup D:\Backup

# 定时备份：每天 20:00 自动执行（程序保持运行，错过自动补跑）
build\backupapp.exe backup --source D:\MyData --target D:\Backup --schedule 20:00

# 查看历史
build\backupapp.exe history --target D:\Backup

# 运行测试
build\backup_tests.exe
```

## 备份目录布局

```
<target>/
├── manifest.txt    最新一次备份的完整清单（增量检测 / 恢复 / 校验的数据基础）
│                   原子写入（先写 .tmp 再 MoveFileEx 替换），失败/取消不覆盖
├── data/           文件数据（保持源相对目录结构，增量备份时删除的文件旧数据会被清理）
├── history.log     历史执行记录（追加）
└── logs/
    └── backup.log  运行日志
```

### Manifest 格式

```
# BackupSystem Manifest
version=1
backup_id=20260904-202530
source=D:\MyData
created=2026-09-04 20:25:30
type=full
file_count=5

[file]
path=a.txt
type=0
size=11
mtime=1757000000
mtime_100ns=133800000000000000
hash=...
data=a.txt
```

- `type=0` 文件，`type=1` 目录，`type=2` 符号链接（默认跳过不入库）
- `mtime_100ns`：Windows FILETIME 100ns 精度，用于同秒内内容变化的增量检测

## 增量备份原理（两级变更检测）

1. **元数据快速判断**：路径是否在旧 Manifest → 不在则新增；比较大小、修改时间（秒级 + 100ns 精度）。
2. **Hash 二次确认**：大小相同但秒级修改时间变化时，才计算 SHA-256 确认是否真正变化，
   避免无谓的文件内容读取。
3. **类型变化处理**：同路径文件↔目录互换时，按"删除旧条目 + 新增"处理，并清理 data/ 下对应旧数据。

## 数据安全保障设计

| 场景 | 保障机制 |
|------|----------|
| 备份中途失败/取消 | 不覆盖旧 Manifest，保留上一次完整清单 |
| 写入中途断电/崩溃 | Manifest 原子写入（临时文件 + MoveFileEx 替换） |
| 文件复制中途失败 | 先写同目录临时文件，成功后原子替换，不截断原文件 |
| 恢复覆盖模式 | 不再"先删后写"，失败时原文件保留 |
| 目标在源目录内/等于源 | 启动时校验并拒绝，防止自我膨胀 |
| junction/符号链接成环 | 默认跳过 reparse point，记录为警告 |
| 子目录无权访问 | 扫描错误计入失败，不返回"成功但缺文件" |
| 备份仓库损坏 | `verify` 命令独立校验 Manifest 与 data/ 一致性 |
| 长路径（>260 字符） | 自动加 `\\?\` 前缀 |

## 代码规范约定

- 核心业务全部 C++，Windows 文件操作集中在 `FileSystem`（Win32 API 封装）。
- Windows HANDLE 使用 RAII（`FileHandle`），避免异常路径资源泄漏。
- `BackupManager` 只负责组织流程，不承载扫描/比较/文件读写等细节（避免巨型类）。
- 单文件失败记录错误并继续，最终在结果中统计成功/失败数量。
- 所有路径比较使用 `CompareStringOrdinal`（大小写不敏感，非 ASCII 可靠）。
