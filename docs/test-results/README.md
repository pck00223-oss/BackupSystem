# 测试结果汇总

- 生成时间：2026-11-06
- 测试环境：Windows 10/11，MinGW-w64 g++ 16.1.0，CMake 4.4.3 + Ninja 1.13.2，C++17
- 测试框架：GoogleTest v1.15.2

## 1. 单元测试（unit-tests.log）

| 指标 | 数值 |
|---|---:|
| 测试套件 | 27 |
| 测试用例总数 | 112 |
| 通过 | 110 |
| 条件跳过 | 2 |
| 失败 | 0 |
| 耗时 | 约 2.8 s |

跳过的 2 个用例（`TarPacker.SymlinkPreserved`、`FileScanner.SymlinkIsRecordedWithWarning`）
均因本机未开启开发者模式/无管理员权限，`CreateSymbolicLinkW` 返回 1314，与功能逻辑无关。

## 2. 端到端测试（e2e-tests.log）

`scripts/test-backup-demo.ps1` 在三种加密模式下全部通过：

| 模式 | 结果 |
|---|---|
| none（不加密） | 全部测试通过（ALL PASS） |
| aes256（AES-256-CBC） | 全部测试通过（ALL PASS） |
| 3des（3DES-CBC） | 全部测试通过（ALL PASS） |

端到端流程：生成边界数据（空文件/块边界 1~33 字节/2MB 大文件/特殊后缀/中文/硬链接）
→ 全量备份 → verify → 新增/修改/删除 → 增量备份 → verify → 恢复 → 逐文件 SHA-256 比对。

## 3. 静态检查

- 编译：严格选项 `-Wall -Wextra -Wconversion -Wshadow -Wsign-conversion -Wpedantic`，0 警告；
- cppcheck：`--enable=warning,style,performance,portability`，0 告警。

## 复现方式

```powershell
# 单元测试
.\build\backup_tests.exe
# 端到端（三种模式）
powershell -ExecutionPolicy Bypass -File scripts\test-backup-demo.ps1 -Encryption none
powershell -ExecutionPolicy Bypass -File scripts\test-backup-demo.ps1 -Encryption aes256
powershell -ExecutionPolicy Bypass -File scripts\test-backup-demo.ps1 -Encryption 3des -Cleanup
```
