# BackupSystem - 基于 C++ / Windows 的数据备份软件

面向软件工程的本地数据备份工具。采用**分层架构 + 面向对象设计**，实现
全量备份、增量备份、数据恢复、Manifest 增量检测、SHA-256 校验、任务调度、日志与异常处理。

## 功能对照（需求文档 → 实现状态）

| 功能 | 状态 | 说明 |
|------|------|------|
| 全量备份 | ✅ 已完成 | 首次备份全部文件 |
| 增量备份 | ✅ 已完成 | 元数据快速判断 + Hash 二次确认（两级变更检测） |
| 数据恢复 | ✅ 已完成 | 恢复后 Hash 校验 + 元数据（修改时间） |
| Manifest | ✅ 已完成 | 文本格式，可解释，UTF-8 |
| Hash 校验 | ✅ 已完成 | 自实现 SHA-256，无第三方依赖 |
| 自定义筛选 | ✅ 已完成 | 扩展名 / 路径 / 大小 / 修改时间 / 空文件 |
| 任务调度 | ✅ 已完成 | 立即执行 + 每天 HH:MM 定时 |
| 备份历史 | ✅ 已完成 | history.log 追加记录 |
| 日志 | ✅ 已完成 | INFO / WARNING / ERROR，文件 + 控制台 |
| 异常处理 | ✅ 已完成 | 单个文件失败不中断，汇总统计 |
| 任务取消 | ✅ 已完成 | BackupTask::cancel() |
| 测试 | ✅ 已完成 | 单元 + 集成测试（不依赖第三方框架） |
| 打包 / 压缩 / AES | ⏳ 扩展预留 | 对应文档 Phase 5/7/8，接口已预留 |

## 架构（分层）

```
CLI / GUI (应用层)
      ↓
Application 层  BackupTask / TaskScheduler / ConfigLoader
      ↓
Business 层     BackupManager / RestoreManager / Manifest / FileComparator / FileFilter
      ↓
File Engine 层  FileScanner / FileCopier / FileSystem
      ↓
Windows API     CreateFileW / FindFirstFileW / ReadFile / WriteFile ...
```

## 目录结构

```
BackupSystem/
├── CMakeLists.txt
├── README.md
├── 数据备份软件设计与实现方案.md   ← 需求文档
├── include/   头文件（按层分包：core / engine / business / app）
├── src/       实现文件（main.cpp 为命令行入口）
├── tests/     单元与集成测试
├── config/    default.conf 默认配置
└── docs/
```

## 构建（Windows，需要 CMake + 编译器）

支持 MSVC 与 MinGW-w64（推荐 MinGW-w64，含 Ninja）。

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

# 定时备份：每天 20:00 自动执行（程序保持运行）
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
├── data/           文件数据（保持源相对目录结构）
├── history.log     历史执行记录（追加）
└── logs/
    └── backup.log  运行日志
```

## 增量备份原理（两级变更检测）

1. **元数据快速判断**：路径是否在旧 Manifest → 不在则新增；比较大小、修改时间。
2. **Hash 二次确认**：大小相同但修改时间变化时，才计算 SHA-256 确认是否真正变化，
   避免无谓的文件内容读取。

## 代码规范约定

- 核心业务全部 C++，Windows 文件操作集中在 `FileSystem`（Win32 API 封装）。
- Windows HANDLE 使用 RAII（`FileHandle`），避免异常路径资源泄漏。
- `BackupManager` 只负责组织流程，不承载扫描/比较/文件读写等细节（避免巨型类）。
- 单文件失败记录错误并继续，最终在结果中统计成功/失败数量（对应面试问题 24.6）。
