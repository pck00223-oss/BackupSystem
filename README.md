# BackupSystem - 基于 C++/Win32 的本地数据备份软件

![Build](https://github.com/pck00223-oss/BackupSystem/actions/workflows/ci.yml/badge.svg)
![Tests](https://img.shields.io/badge/tests-386-brightgreen)
![License](https://img.shields.io/badge/license-MIT-blue)

面向 Windows 平台的本地数据备份工具。采用**分层架构 + 面向对象设计**，零第三方依赖，
实现全量/增量备份、数据恢复、完整性校验、快照与时间点恢复、AES-256 加密、Win32 原生 GUI、
Windows 计划任务集成等完整功能。

## 功能亮点

| 功能 | 说明 |
|------|------|
| 全量/增量备份 | 两级变更检测（元数据快速判断 + SHA-256 二次确认），100ns 精度 mtime 覆盖同秒修改 |
| 数据恢复 | 恢复后 Hash 校验，冲突时可选覆盖/跳过，支持从指定快照时间点恢复 |
| 完整性校验 | `verify` 子命令按 Manifest 校验 data/ 与全部快照，报告缺失/损坏；`--repair` 自动重建损坏条目 |
| 快照与时间点恢复 | `--keep-snapshots N` 保留最近 N 份完整快照（硬链接节省空间），超过自动清理最旧 |
| AES-256 加密 | 自实现 AES-256-CBC，BCryptGenRandom 安全 IV，1MB 分块流式加密（大文件不爆内存），NIST 标准向量验证通过 |
| Win32 原生 GUI | `backupgui.exe` 零依赖桌面界面：任务列表、新建/备份/恢复/校验、实时日志与进度、任务配置持久化（DPAPI 加密密码） |
| Windows 计划任务 | `schedule --register` 注册为系统计划任务，到点自动备份完成后退出，无需程序常驻 |
| 单一实例锁 | 防止计划任务与手动备份同时写同一 target |
| 崩溃恢复 | 类型互换时残留 `.baktmp.old` 自动检测与还原，verify 报告异常残留 |

## 技术栈

- **语言**：C++17
- **平台**：Windows（Win32 API，零第三方依赖）
- **构建**：CMake + Ninja / MSVC
- **加密**：自实现 AES-256-CBC + SHA-256（NIST 标准向量验证），BCryptGenRandom 生成 IV
- **测试**：自研轻量测试框架（无依赖），386 项断言
- **CI**：GitHub Actions（strict 构建 + 全部测试 + cppcheck + 加固构建）

## 架构设计（分层）

```
CLI (backupapp) / GUI (backupgui)      ← 应用层
      ↓
Application 层  TaskStore / TaskScheduler / ConfigLoader / ScheduleManager
      ↓
Business 层     BackupManager / RestoreManager / VerifyManager
                Manifest / FileComparator / FileFilter / SnapshotManager
      ↓
File Engine 层  FileScanner / FileCopier / FileSystem / HashCalculator / AesEncryptor
      ↓
Windows API     CreateFileW / FindFirstFileW / ReadFile / WriteFile / MoveFileExW / BCryptGenRandom
```

**设计原则**：
- `BackupManager` 只负责组织流程，不承载扫描/比较/文件读写细节
- Windows 文件操作集中在 `FileSystem`（Win32 API 封装），HANDLE 使用 RAII
- 单文件失败记录错误并继续，最终在结果中统计成功/失败数量
- 所有路径比较使用 `CompareStringOrdinal`（大小写不敏感，非 ASCII 可靠）

## 核心难点与解决方案

| 难点 | 问题 | 解决方案 |
|------|------|----------|
| 静默不完整备份 | 子目录无权访问时扫描返回成功但缺文件；单文件失败后仍覆盖 Manifest | 扫描错误计入失败；失败/取消不覆盖旧 Manifest，保留上一次完整清单 |
| 原子写入 | 写入中途断电/崩溃留下截断文件，恢复覆盖时先删后写导致原文件丢失 | FileCopier 先写同目录临时文件，成功后 `MoveFileEx` 原子替换；恢复端取消先删后写 |
| 加密仓库 verify 误报 | Manifest 记录明文 Hash/大小，data/ 存 IV+密文，verify 直接比较必然全部报损坏 | Manifest 新增 `cipher_size`/`cipher_hash` 字段，verify 基于密文校验；明文 Hash 仅用于增量判断与恢复校验 |
| 临时文件撞名合法数据 | 临时文件固定 `dst + ".baktmp"`，用户源目录恰好有同名文件时被误删 | 改用 `GetTempFileNameW` 生成唯一临时名；崩溃恢复/残留检测基于 Manifest 白名单，只有不在清单中的对象才当作残留处理 |
| junction 无限递归 | 目录联接点指向自身祖先时递归成环，指向 C:\ 则备份超出预期内容 | reparse point 优先判定，默认跳过所有 junction/符号链接，记录为警告 |
| 同秒内容变化漏报 | mtime 存 Unix 秒，文件一秒内修改且大小不变时增量永远检测不到 | 保留 100ns 精度 FILETIME，大小相同时比较 100ns mtime，仍相同则 Hash 二次确认 |
| 混血加密仓库 | 增量备份时换加密方式/密码，未变化文件沿用旧加密，变化文件用新加密，恢复时部分解不开 | 增量前校验 encryption + passwordVerifier 与上一份一致，不一致则要求全量重建；全量模式允许切换但强制重新写入所有文件 |
| 大文件加密内存溢出 | 整文件读入内存再加密，视频/镜像文件同时占用源+密文多份内存 | AES 加密/解密改为 1MB 分块流式处理 |

## 目录结构

```
BackupSystem/
├── .github/workflows/ci.yml    GitHub Actions CI（strict 构建 + 测试 + cppcheck + 加固构建）
├── CMakeLists.txt
├── README.md
├── 数据备份软件设计与实现方案.md   ← 需求文档
├── include/   头文件（按层分包：core / engine / business / app / gui）
├── src/       实现文件（main.cpp CLI 入口 / gui_main.cpp GUI 入口）
├── tests/     单元与集成测试（386 项断言）
├── config/    default.conf 默认配置
└── scripts/   run_checks.ps1 一键健壮性检查脚本
```

## 构建

需要 CMake + MinGW-w64（推荐）或 MSVC。

```bash
# MinGW-w64 + Ninja
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# MSVC (Visual Studio)
cmake -S . -B build-msvc -G "Visual Studio 17 2022"
cmake --build build-msvc --config Release
```

构建产物：
- `build/backupapp.exe` — 命令行主程序
- `build/backupgui.exe` — Win32 原生 GUI
- `build/backup_tests.exe` — 测试程序

### 健壮性检查构建

```bash
# 严格编译警告（-Wconversion -Wshadow -Wsign-conversion 等）
cmake -S . -B build-strict -G Ninja -DSTRICT_WARNINGS=ON -DENABLE_CPPCHECK=ON

# 运行时加固（libstdc++ 调试模式 + 栈保护 + 整数溢出 trap）
cmake -S . -B build-hard -G Ninja -DHARDENING=ON
```

## 快速使用

```bash
# 全量备份
build\backupapp.exe backup --source D:\MyData --target D:\Backup --type full

# 增量备份（只处理变化文件）
build\backupapp.exe backup --source D:\MyData --target D:\Backup --type incremental

# 保留最近 5 份快照
build\backupapp.exe backup --source D:\MyData --target D:\Backup --type full --keep-snapshots 5

# AES-256 加密备份
build\backupapp.exe backup --source D:\MyData --target D:\Backup --type full --encrypt aes256 --password mysecret

# 恢复（可选 --overwrite 覆盖已存在文件，默认跳过）
build\backupapp.exe restore --backup D:\Backup --to E:\Restored --overwrite

# 从指定快照恢复（时间点恢复）
build\backupapp.exe restore --backup D:\Backup --to E:\Restored --snapshot 20260905-001128

# 完整性校验（含全部快照）
build\backupapp.exe verify --backup D:\Backup --all-snapshots

# 校验 + 自动修复（用源文件重建损坏条目）
build\backupapp.exe verify --backup D:\Backup --repair --source D:\MyData

# 注册为 Windows 计划任务（每天 20:00 自动备份，需管理员）
build\backupapp.exe schedule --register --time 20:00 --source D:\MyData --target D:\Backup --type full

# 启动 GUI
build\backupgui.exe

# 运行测试
build\backup_tests.exe
```

## 备份目录布局

```
<target>/
├── manifest.txt       最新一次备份的完整清单（原子写入，失败/取消不覆盖）
├── data/              文件数据（保持源相对目录结构）
├── snapshots/         快照目录（--keep-snapshots N > 0 时创建）
│   └── <timestamp>/   每份快照含独立 manifest.txt 和 data/（未变化文件硬链接节省空间）
├── history.log        历史执行记录（追加）
└── logs/
    └── backup.log     运行日志
```

## 数据安全保障

| 场景 | 保障机制 |
|------|----------|
| 备份中途失败/取消 | 不覆盖旧 Manifest，保留上一次完整清单 |
| 写入中途断电/崩溃 | Manifest 原子写入（临时文件 + MoveFileEx 替换） |
| 文件复制中途失败 | 先写同目录临时文件，成功后原子替换，不截断原文件 |
| 恢复覆盖模式 | 不再"先删后写"，失败时原文件保留 |
| 目标在源目录内/等于源 | 启动时校验并拒绝，防止自我膨胀 |
| junction/符号链接成环 | 默认跳过 reparse point，记录为警告 |
| 子目录无权访问 | 扫描错误计入失败，不返回"成功但缺文件" |
| 备份仓库损坏 | verify 独立校验 Manifest 与 data/ 及全部快照一致性 |
| 类型互换崩溃残留 | `.baktmp.old` 自动检测与还原，verify 报告异常 |
| 并发写入冲突 | 单一实例锁（`.backup.lock` + PID 存活检测），防止计划任务与手动备份同时写 |
| 长路径（>260 字符） | 自动加 `\\?\` 前缀 |

## 测试与 CI

- **386 项断言**全部通过，覆盖：备份/恢复/校验/增量检测/加密/NIST 标准向量/快照/崩溃恢复/GUI 任务持久化/大文件流式加密
- **GitHub Actions CI**：每次 push 自动执行 strict 构建（0 警告）+ 全部测试 + cppcheck（0 告警）+ 加固构建
- 严格编译选项：`-Wall -Wextra -Wconversion -Wshadow -Wsign-conversion -Wpedantic`

## 加密兼容性说明

1. commit `3f93cb9` 之前生成的加密备份 Manifest 没有 `cipher_size`/`cipher_hash` 字段，
   用新版本 verify 会报损坏；解决方法：用新版本做一次全量备份重建仓库。
2. `verify --repair` 对加密文件不生效（只报告损坏），因为无法从明文源文件直接重建密文；
   密文损坏时需重新备份该文件（修改源文件触发增量，或全量重建）。
