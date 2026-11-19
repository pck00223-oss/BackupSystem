# BackupSystem Qt6 界面实现说明

> 本文记录 BackupSystem 从纯 Win32 界面迁移到 Qt6 界面的设计与实现。
> 核心原则：**业务逻辑零改动，只重写表现层**；Qt 与 Win32 两套界面共用同一套核心库。

---

## 1. 背景与目标

项目早期的图形界面使用纯 Win32 API 手写（零第三方依赖），但原生控件外观简单、布局能力弱、
手动管理窗口消息成本高。为获得更现代的界面与更高效的 UI 开发方式，将表现层用 **Qt6** 重写。

重写目标：

- **不碰业务逻辑**：备份、恢复、校验、压缩、加密、快照等核心代码 100% 复用；
- **只替换表现层**：用 Qt 控件实现任务管理、对话框、进度与日志；
- **保留 Win32 版**：作为无 Qt 环境下的回退，两个目标在 CMake 中并存。

---

## 2. 分层与复用策略

系统整体为分层架构，依赖只能向下。Qt 重写只发生在最上层：

```
┌─────────────────────────────────────────────┐
│  表现层（本次重写）                            │
│   Win32: src/gui/*        Qt: src/gui/qt/*    │
├─────────────────────────────────────────────┤
│  应用层 src/app      任务封装/配置/调度          │
│  业务层 src/business 备份/恢复/校验/快照/筛选    │
│  核心层 src/core     压缩/加密/文件/锁/日志       │
└─────────────────────────────────────────────┘
```

复用边界：

| 组件 | 是否复用 | 说明 |
|---|---|---|
| `backup_core`（全部业务与核心逻辑） | 完全复用 | 静态库，Qt 直接链接 |
| `TaskStore`（任务持久化 + DPAPI） | 完全复用 | 纯 C++，与 Qt 无关 |
| Win32 界面（`src/gui` 非 qt 部分） | 保留 | 独立目标 `backupgui`，作为回退 |
| Qt 表现层（`src/gui/qt`） | 全新编写 | 仅这一层是新代码 |

因为核心测试针对的是 `backup_core`，重写表现层不影响任何核心单元测试。

---

## 3. 技术栈与构建

- **Qt 版本**：Qt 6.8.3 LTS（MinGW 64 位构建）
- **编译器**：MinGW-w64 g++ 13.1.0（posix-seh）
- **构建系统**：CMake，`find_package(Qt6 COMPONENTS Widgets)` 可选启用
- **元对象**：目标开启 `AUTOMOC`，自动处理 `Q_OBJECT` 的 moc 生成
- **部署**：`windeployqt` 收集平台插件与运行时 DLL

CMake 中 Qt 目标为可选——找不到 Qt6 时自动跳过，Win32 目标仍可构建：

```cmake
find_package(Qt6 QUIET COMPONENTS Widgets)
if(Qt6_FOUND)
    add_executable(backupgui_qt WIN32 ...)
    set_target_properties(backupgui_qt PROPERTIES AUTOMOC ON)
    target_link_libraries(backupgui_qt PRIVATE backup_core backup_flags
                                               Qt6::Widgets crypt32)
endif()
```

> 链接 `crypt32` 是因为复用的 TaskStore 使用 Windows DPAPI 加密任务密码。

---

## 4. 组件设计

Qt 表现层共 4 个主要类，职责单一：

### 4.1 MainWindow（主窗口）

- 继承 `QMainWindow`，承载整体界面；
- 左侧：任务表格（`QTableWidget`，6 列：名称/源/目标/模式/压缩/加密）与操作按钮；
- 右侧：进度条、状态标签、深色日志面板（`QPlainTextEdit`）；
- 用 `QSplitter` 做左右分栏，QSS 实现现代卡片风格；
- 负责新建/编辑/删除任务、触发备份/恢复/校验，以及任务的加载与保存。

### 4.2 TaskDialog（新建/编辑对话框）

- 继承 `QDialog`，收集一个任务的完整配置；
- 字段：任务名、源目录、目标目录、备份模式（全量/增量）、**压缩（不压缩/Huffman/RLE）**、
  加密（不加密/AES-256/3DES）、密码、保留快照数；
- `setTask()` 用已有任务填充控件，`result()` 读取控件并做校验；
- 字段与校验规则与 Win32 版对话框保持一致。

### 4.3 Worker（后台执行线程）

- 继承 `QThread`，在子线程调用核心库，避免耗时操作阻塞 UI；
- 内部按操作类型分发：`runBackup()` / `runRestore()` / `runVerify()`；
- 通过**信号**把日志和结果回传主线程：
  - `logMessage(QString, level)`：日志文本与级别（决定颜色）；
  - `taskFinished(bool, QString)`：成功与否与汇总；
- 用 `std::atomic<bool>` 实现取消标志，核心库的回调中检查该标志以中止任务；
- 本类只做线程封装与字符串/信号转换，**不含任何业务逻辑**。

### 4.4 TaskStore（任务持久化，复用）

- 任务列表保存到 `%APPDATA%\BackupSystem\tasks.dat`；
- UTF-8 文本、制表符分隔；密码先用 **DPAPI 加密**再十六进制编码（仅当前用户可解密）；
- 共 8 个字段，末位为压缩方式；旧文件缺少该列时按"不压缩"处理，向后兼容。

---

## 5. 线程模型与通信

Qt 的 UI 控件只能在主线程操作，而备份/恢复可能耗时较长，因此采用**线程 + 信号槽**：

```
主线程 (UI)                         子线程 (Worker : QThread)
─────────────                       ─────────────────────────
配置任务、start()  ───────────────▶  run()
                                     调用 BackupManager/RestoreManager/...
appendLog()  ◀──── logMessage 信号 ──  核心进度回调 → emitLog
更新状态      ◀──── taskFinished 信号 ─  执行结束
```

要点：

- 子线程**不直接操作控件**，只发信号；主线程通过槽函数更新界面，线程安全；
- 取消通过原子标志传递，核心库的 `cancelCheck` 回调读取它；
- 日志级别（Info/Success/Warn/Error）映射为日志面板的不同颜色。

---

## 6. 字符串与编码转换

核心库使用 `std::wstring`（Windows 宽字符），Qt 使用 `QString`，在边界处转换：

- 路径：`QString::fromStdWString()` / `QString::toStdWString()`；
- 密码等 UTF-8 字节串：`QString::toUtf8().toStdString()` / `QString::fromUtf8()`；
- 转换集中在 Worker 与对话框边界，业务代码内部不感知 Qt 类型。

---

## 7. 功能覆盖与已知缺口

### 已在 Qt 接通

- 全量 / 增量备份；
- Huffman / RLE 压缩（备份时先压缩后加密，恢复时先解密再解压，均自动完成）；
- AES-256 / 3DES 加密与密码；
- 恢复（选目录、覆盖策略、自动解密解压、Hash 校验）；
- 完整性校验（根目录 + 遍历所有快照）；
- 快照的自动创建与过期清理（由保留快照数驱动，核心内部自动处理）。

### 未在 Qt 暴露的高级入口

| 能力 | 现状 | 命令行是否支持 |
|---|---|---|
| 自定义筛选（扩展名/大小/修改时间） | 未接，默认全备份 | 支持 `--include-ext` / `--exclude-ext` |
| 从指定历史快照恢复 | 未接，只能从最新恢复 | 支持 `--snapshot` |
| 校验自动修复（repair） | 未接 | 支持 `--repair --source` |
| 定时调度 | 未接 | 支持 `schedule` |

---

## 8. 构建与运行步骤

```powershell
# 1. 配置（MinGW 工具链路径前置）
cmake -S . -B build-qt -G "MinGW Makefiles"

# 2. 构建
cmake --build build-qt --target backupgui_qt

# 3. 部署 Qt 运行时（拷贝 exe 到其他机器前必须执行）
windeployqt build-qt/backupgui_qt.exe

# 4. 运行
./build-qt/backupgui_qt.exe
```

> 注意：`windeployqt` 部署后，exe 同目录会包含 platforms/styles 等插件子目录，
> 单独拷贝 exe 会因缺少平台插件而无法启动。
