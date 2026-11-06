# test-backup-demo.ps1 - BackupSystem 端到端测试脚本（自动生成测试数据）
#
# 自动完成：
#   1. 生成覆盖边界情况的测试源目录（空文件/块边界/大文件/特殊后缀/中文/硬链接/子目录）
#   2. 全量备份 -> verify 完整性校验
#   3. 模拟新增/修改/删除 -> 增量备份 -> verify
#   4. 恢复到独立目录 -> 逐文件 Hash 比对
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File scripts\test-backup-demo.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\test-backup-demo.ps1 -Encryption aes256
#   powershell -ExecutionPolicy Bypass -File scripts\test-backup-demo.ps1 -Encryption 3des -Cleanup
#
# 参数：
#   -WorkRoot    测试工作目录（默认 D:\backup_demo_test）
#   -Encryption  none / aes256 / 3des（默认 none）
#   -Password    加密密码（默认 Demo@12345）
#   -Cleanup     结束后删除全部测试数据
[CmdletBinding()]
param(
    [string]$WorkRoot = 'D:\backup_demo_test',
    [ValidateSet('none', 'aes256', '3des')]
    [string]$Encryption = 'none',
    [string]$Password = 'Demo@12345',
    [switch]$Cleanup
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$AppExe = Join-Path $projectRoot 'build\backupapp.exe'
if (-not (Test-Path $AppExe)) { throw "找不到 $AppExe，请先 cmake --build build" }

$src = Join-Path $WorkRoot 'source'
$bak = Join-Path $WorkRoot 'backup'
$restore = Join-Path $WorkRoot 'restored'

$script:failures = 0
function Step([string]$msg) { Write-Host "`n===== $msg =====" -ForegroundColor Cyan }
function Check([bool]$cond, [string]$msg) {
    if ($cond) { Write-Host "  [PASS] $msg" -ForegroundColor Green }
    else { Write-Host "  [FAIL] $msg" -ForegroundColor Red; $script:failures++ }
}
# 写入恰好 n 字节的文件（确定性内容）
function Write-Bytes([string]$path, [int]$n) {
    $b = [byte[]]::new($n)
    for ($i = 0; $i -lt $n; $i++) { $b[$i] = [byte](($i * 31 + 7) -band 0xFF) }
    [IO.File]::WriteAllBytes($path, $b)
}
# 调用 backupapp，返回是否成功（退出码 0）；原生命令输出丢弃，只返回布尔结果
function Invoke-App {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$ArgList)
    & $AppExe @ArgList | Out-Null
    return ($LASTEXITCODE -eq 0)
}

# ============ 0. 清理并准备目录 ============
if (Test-Path $WorkRoot) { Remove-Item -Recurse -Force $WorkRoot }
$null = New-Item -ItemType Directory -Force -Path $src, (Join-Path $src 'subdir'), (Join-Path $src 'subdir\deep')

Step '1. 生成测试源数据（覆盖边界）'
[IO.File]::WriteAllText((Join-Path $src 'a.txt'), 'hello world')
[IO.File]::WriteAllText((Join-Path $src 'subdir\b.txt'), 'nested file')
[IO.File]::WriteAllText((Join-Path $src 'subdir\deep\c.txt'), 'deep nested')
Write-Bytes (Join-Path $src 'empty.txt') 0                 # 空文件
# 块边界：同时覆盖 AES(16) 与 3DES(8) 的关键长度
foreach ($n in 1, 7, 8, 15, 16, 17, 31, 32, 33) {
    Write-Bytes (Join-Path $src ("boundary_{0}.bin" -f $n)) $n
}
Write-Bytes (Join-Path $src 'large_2mb_plus37.bin') (2 * 1024 * 1024 + 37)  # 跨 1MB 分块
# 特殊后缀：合法文件，不得被崩溃恢复/verify 当作残留
[IO.File]::WriteAllText((Join-Path $src 'data.baktmp'), 'legal baktmp file')
[IO.File]::WriteAllText((Join-Path $src 'note.baktmp.old'), 'legal baktmp old')
# 中文文件名与内容
[IO.File]::WriteAllText((Join-Path $src '中文文件.txt'), '我爱学习，备份测试')
# 硬链接：两个名字指向同一文件（同 inode）
[IO.File]::WriteAllText((Join-Path $src 'hard_original.txt'), 'hard link content')
$null = New-Item -ItemType HardLink -Path (Join-Path $src 'hard_link.txt') -Target (Join-Path $src 'hard_original.txt')
# 待删除文件（增量阶段删除）
[IO.File]::WriteAllText((Join-Path $src 'to_delete.txt'), 'will be removed')

$srcCount = (Get-ChildItem $src -Recurse -File).Count
Write-Host "  源文件数: $srcCount"

# ============ 2. 全量备份 ============
Step '2. 全量备份'
$encArgs = @()
if ($Encryption -ne 'none') { $encArgs = @('--encrypt', $Encryption, '--password', $Password) }
$ok = Invoke-App backup --source $src --target $bak --type full @encArgs
Check $ok "全量备份成功（encryption=$Encryption）"
Check (Test-Path (Join-Path $bak 'manifest.txt')) 'manifest.txt 已生成'

# ============ 3. verify ============
Step '3. 完整性校验 verify'
$out = & $AppExe verify --backup $bak 2>&1 | Out-String
Check ($LASTEXITCODE -eq 0) 'verify 退出码 0'
Check ($out -match '缺失\s*:\s*0') '缺失 0'
Check ($out -match '损坏\s*:\s*0') '损坏 0'
Check ($out -match '残留\s*:\s*0') '残留 0（合法 .baktmp 未误判）'
Check (Test-Path (Join-Path $bak 'data\data.baktmp')) '合法 data.baktmp 备份仍在'

# ============ 4. 模拟变更 ============
Step '4. 模拟新增 / 修改 / 删除'
[IO.File]::WriteAllText((Join-Path $src 'new_file.txt'), 'brand new file')   # 新增
[IO.File]::WriteAllText((Join-Path $src 'a.txt'), 'hello modified content')  # 修改
Remove-Item (Join-Path $src 'to_delete.txt') -Force                          # 删除

# ============ 5. 增量备份 ============
Step '5. 增量备份'
$ok = Invoke-App backup --source $src --target $bak --type incremental @encArgs
Check $ok '增量备份成功'

# ============ 6. 增量后 verify ============
Step '6. 增量后 verify'
$out = & $AppExe verify --backup $bak 2>&1 | Out-String
Check ($LASTEXITCODE -eq 0) 'verify 退出码 0'
Check ($out -match '缺失\s*:\s*0') '缺失 0'
Check ($out -match '损坏\s*:\s*0') '损坏 0'
Check ($out -match '残留\s*:\s*0') '残留 0'

# ============ 7. 恢复 ============
Step '7. 恢复到独立目录'
$restArgs = @('--password', $Password)
$ok = Invoke-App restore --backup $bak --to $restore --overwrite @restArgs
Check $ok '恢复成功'

# ============ 8. 逐文件比对 ============
Step '8. 恢复内容比对'
$curFiles = Get-ChildItem $src -Recurse -File
$restFiles = Get-ChildItem $restore -Recurse -File
Check ($restFiles.Count -eq $curFiles.Count) "文件数一致（$($curFiles.Count)）"
$mismatch = 0
foreach ($f in $curFiles) {
    $rel = $f.FullName.Substring($src.Length)
    $target = Join-Path $restore $rel
    if (-not (Test-Path $target)) { $mismatch++; continue }
    $h1 = (Get-FileHash $f.FullName -Algorithm SHA256).Hash
    $h2 = (Get-FileHash $target -Algorithm SHA256).Hash
    if ($h1 -ne $h2) { $mismatch++ }
}
Check ($mismatch -eq 0) '逐文件 SHA-256 全部一致'
Check (-not (Test-Path (Join-Path $restore 'to_delete.txt'))) '已删除文件未被恢复'
Check (Test-Path (Join-Path $restore 'new_file.txt')) '新增文件已恢复'
Check ([IO.File]::ReadAllText((Join-Path $restore '中文文件.txt')) -eq '我爱学习，备份测试') '中文内容正确'
Check (Test-Path (Join-Path $restore 'data.baktmp')) '特殊后缀文件完整恢复'

# ============ 汇总 ============
Write-Host "`n========================================" -ForegroundColor Yellow
if ($script:failures -eq 0) {
    Write-Host '全部测试通过（ALL PASS）' -ForegroundColor Green
} else {
    Write-Host "有 $script:failures 项失败（FAIL）" -ForegroundColor Red
}
Write-Host "加密方式: $Encryption    工作目录: $WorkRoot"
Write-Host "========================================`n" -ForegroundColor Yellow

if ($Cleanup) { Remove-Item -Recurse -Force $WorkRoot; Write-Host '已清理测试数据' }
exit ([int]($script:failures -gt 0))
