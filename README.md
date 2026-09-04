# BackupSystem - 基于 C++ / Windows 的数据备份软件

面向软件工程 / 计算机复试的本地数据备份工具。采用**分层架构 + 面向对象设计**，实现
全量备份、增量备份、数据恢复、完整性校验、Manifest 增量检测、SHA-256 校验、任务调度、日志与异常处理。

## 功能对照（需求文档 → 实现状态）

| 功能 | 状态 | 说明 |
|------|------|------|
| 全量备份 | ✅ 已完成 | 首次备份全部文件（含空目录） |
| 增量备份 | ✅ 已完成 | 元数据快速判断 + Hash 二次确认（两级变更检测，100ns 精度时间） |
| 数据恢复 | ✅ 已完成 | 恢复后 Hash 校验 + 元数据（修改时间），冲突跳过记为警告 |
| 完整性校验 | ✅ 已完成 | `verify` 子命令：按 Manifest 校验 data/ 仓库，报告缺失/损坏；`--repair` 可自动用源文件重建损坏条目 |
| 快照与时间点恢复 | ✅ 已完成 | `--keep-snapshots N` 保留最近 N 份完整快照（硬链接节省空间），`restore --snapshot <timestamp>` 从指定快照恢复，超过 N 份自动清理最旧 |
| AES-256 加密 | ✅ 已完成 | `--encrypt aes256 --password <密码>` 备份时 AES-256-CBC 加密（IV+密文，SHA-256 派生密钥，自实现无第三方依赖），恢复时自动解密；空文件可加密 |
| Win32 GUI | ✅ 已完成 | `backupgui.exe` 原生桌面界面（零第三方依赖）：任务列表、新建任务、立即备份、恢复、校验、实时日志与进度 |
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

# 保留最近 5 份快照（硬链接节省空间，超过 5 份自动清理最旧）
# 注意：改小 --keep-snapshots 或改成 0 不会主动删除已有快照，下次备份后按新值清理
build\backupapp.exe backup --source D:\MyData --target D:\Backup --type full --keep-snapshots 5

# AES-256 加密备份（自实现 AES，无第三方依赖；data/ 中存储 IV+密文，Manifest 记录原始 Hash/大小 + 密文 Hash/大小）
build\backupapp.exe backup --source D:\MyData --target D:\Backup --type full --encrypt aes256 --password mysecret
# 兼容性说明：
#   1. commit 3f93cb9 之前生成的加密备份 Manifest 没有 cipher_size/cipher_hash 字段，
#      用新版本 verify 会报损坏；解决方法：用新版本做一次全量备份重建仓库。
#   2. verify --repair 对加密文件不生效（只报告损坏），因为无法从明文源文件直接重建密文；
#      密文损坏时需重新备份该文件（修改源文件触发增量，或全量重建）。

# 自定义筛选：只备份 .cpp/.h，最近 30 天修改，大于 1KB
build\backupapp.exe backup --source D:\MyData --target D:\Backup --include-ext .cpp,.h

# 恢复：把备份恢复到 E:\Restored
build\backupapp.exe restore --backup D:\Backup --to E:\Restored

# 从指定快照恢复（时间点恢复，快照列表在 D:\Backup\snapshots\ 下）
build\backupapp.exe restore --backup D:\Backup --to E:\Restored --snapshot 20260905-001128

# 加密备份的恢复（需要相同密码；无密码或错误密码会失败，不会输出残缺文件）
build\backupapp.exe restore --backup D:\Backup --to E:\Restored --password mysecret

# 完整性校验：按 Manifest 检查 data/ 仓库是否有缺失或损坏
build\backupapp.exe verify --backup D:\Backup

# 完整性校验 + 自动修复：发现损坏/缺失时，用源目录文件重建（源文件 Hash 必须与 Manifest 一致）
build\backupapp.exe verify --backup D:\Backup --repair --source D:\MyData

# 只校验指定快照 / 校验所有保留快照（快照只读，不支持 --repair）
build\backupapp.exe verify --backup D:\Backup --snapshot 20260905-001128
build\backupapp.exe verify --backup D:\Backup --all-snapshots

# 注册为 Windows 计划任务（推荐长期自用）：每天 20:00 自动备份，完成后退出，无需程序常驻
# 需要以管理员身份运行
build\backupapp.exe schedule --register --time 20:00 --source D:\MyData --target D:\Backup --type full

# 注意: 任务使用交互式登录令牌, 用户登录/锁屏状态下到点触发; 注销后已排队任务可能执行一次, 未登录时不会主动启动新任务
# 查看计划任务状态 / 卸载
build\backupapp.exe schedule --status
build\backupapp.exe schedule --unregister

# 常驻定时备份（不推荐长期自用）：程序保持运行，错过自动补跑
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
├── snapshots/      快照目录（仅 --keep-snapshots N > 0 时创建）
│   └── <timestamp>/  每份快照含独立的 manifest.txt 和 data/（未变化文件用硬链接节省空间）
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
