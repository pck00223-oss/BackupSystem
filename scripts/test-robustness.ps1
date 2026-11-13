# test-robustness.ps1 - BackupSystem 健壮性/压力测试（模拟老师生成"千奇百怪"的文件）
#
# 目的：在极端文件名、Unicode、长路径、深层目录、二进制内容、分块边界、
#       只读/隐藏属性、大量文件等场景下，验证备份引擎不崩溃、verify 通过、
#       恢复后逐文件内容完全一致。
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File scripts\test-robustness.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\test-robustness.ps1 -Encryption aes256 -Cleanup
#   powershell -ExecutionPolicy Bypass -File scripts\test-robustness.ps1 -StressCount 300
[CmdletBinding()]
param(
    [string]$WorkRoot = 'D:\backup_robust_test',
    [ValidateSet('none', 'aes256', '3des')]
    [string]$Encryption = 'none',
    [string]$Password = 'Robust@12345',
    [int]$StressCount = 100,
    [switch]$Cleanup
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$AppExe = Join-Path $projectRoot 'build\backupapp.exe'
if (-not (Test-Path $AppExe)) { throw "找不到 $AppExe，请先 cmake --build build" }

# 动态链接的 exe 依赖 MinGW 运行时 DLL；若当前 PATH 找不到编译器，自动定位并加入 PATH。
if (-not (Get-Command g++ -ErrorAction SilentlyContinue)) {
    $toolchain = Get-ChildItem 'C:\Users\Administrator\AppData\Local\Microsoft\WinGet\Packages' `
        -Recurse -Filter 'g++.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($toolchain) { $env:PATH = (Split-Path $toolchain.FullName) + ';' + $env:PATH }
}

$src = Join-Path $WorkRoot 'source'
$bak = Join-Path $WorkRoot 'backup'
$restore = Join-Path $WorkRoot 'restored'

$script:failures = 0
function Step([string]$msg) { Write-Host "`n===== $msg =====" -ForegroundColor Cyan }
function Check([bool]$cond, [string]$msg) {
    if ($cond) { Write-Host "  [PASS] $msg" -ForegroundColor Green }
    else { Write-Host "  [FAIL] $msg" -ForegroundColor Red; $script:failures++ }
}
function Write-Bytes([string]$path, [int]$n, [int]$seed = 0) {
    $b = [byte[]]::new($n)
    for ($i = 0; $i -lt $n; $i++) { $b[$i] = [byte](($i * 31 + 7 + $seed) -band 0xFF) }
    [IO.File]::WriteAllBytes($path, $b)
}
function Invoke-App {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$ArgList)
    & $AppExe @ArgList | Out-Null
    return ($LASTEXITCODE -eq 0)
}

# ============ 0. 准备 ============
if (Test-Path $WorkRoot) {
    # 清除只读属性，避免删除失败
    Get-ChildItem $WorkRoot -Recurse -Force -ErrorAction SilentlyContinue |
        Where-Object { -not $_.PSIsContainer } | ForEach-Object { $_.Attributes = 'Normal' }
    Remove-Item -Recurse -Force $WorkRoot
}
$null = New-Item -ItemType Directory -Force -Path $src, (Join-Path $src 'unicode'), (Join-Path $src 'content'), (Join-Path $src 'attrs')

Step '1. 生成特殊文件名'
[IO.File]::WriteAllText((Join-Path $src 'my file.txt'), 'space in name')
[IO.File]::WriteAllText((Join-Path $src '[bracket].txt'), 'square brackets')
[IO.File]::WriteAllText((Join-Path $src '(parens)&and!#@+;.txt'), 'special chars')
[IO.File]::WriteAllText((Join-Path $src 'a.b.c.d.txt'), 'multiple dots')
[IO.File]::WriteAllText((Join-Path $src '.hidden'), 'dot leading')
[IO.File]::WriteAllText((Join-Path $src 'trailing.'), 'dot trailing')
[IO.File]::WriteAllText((Join-Path $src 'noextension'), 'no ext')
[IO.File]::WriteAllText((Join-Path $src '.gitignore'), '*.log')
[IO.File]::WriteAllText((Join-Path $src ('longname_' + ('x' * 180) + '.txt')), 'long file name')

Step '2. 生成 Unicode / 国际化文件名'
[IO.File]::WriteAllText((Join-Path $src 'unicode\中文文件.txt'), '中文内容备份')
[IO.File]::WriteAllText((Join-Path $src 'unicode\日本語ファイル.txt'), '日本語の内容')
[IO.File]::WriteAllText((Join-Path $src 'unicode\emoji😀🎉.txt'), 'emoji content')
[IO.File]::WriteAllText((Join-Path $src 'unicode\混合 mix 123.txt'), 'mixed 混合')

Step '3. 生成内容边界（空/二进制/分块边界）'
Write-Bytes (Join-Path $src 'content\empty.bin') 0
Write-Bytes (Join-Path $src 'content\all_zero.bin') 1024
$ff = [byte[]]::new(1024); for ($i = 0; $i -lt 1024; $i++) { $ff[$i] = 0xFF }
[IO.File]::WriteAllBytes((Join-Path $src 'content\all_ff.bin'), $ff)
$rnd = [byte[]]::new(777); [System.Random]::new(42).NextBytes($rnd)
[IO.File]::WriteAllBytes((Join-Path $src 'content\random.bin'), $rnd)
$mb = 1048576
Write-Bytes (Join-Path $src 'content\exact_1mb.bin') $mb 1
Write-Bytes (Join-Path $src 'content\mb_minus1.bin') ($mb - 1) 2
Write-Bytes (Join-Path $src 'content\mb_plus1.bin') ($mb + 1) 3
Write-Bytes (Join-Path $src 'content\big_3mb.bin') (3 * $mb + 53) 4

Step '4. 深层嵌套目录（制造长路径）'
$deep = $src
for ($i = 1; $i -le 12; $i++) {
    $deep = Join-Path $deep ("level{0}_{1}" -f $i, ('d' * 8))
    $null = New-Item -ItemType Directory -Force -Path $deep
    [IO.File]::WriteAllText((Join-Path $deep 'f.txt'), "depth $i")
}
$null = New-Item -ItemType Directory -Force -Path (Join-Path $src 'empty_dir')

Step '5. 只读 / 隐藏属性文件'
[IO.File]::WriteAllText((Join-Path $src 'attrs\readonly.txt'), 'read only')
(Get-Item (Join-Path $src 'attrs\readonly.txt')).Attributes = 'ReadOnly'
[IO.File]::WriteAllText((Join-Path $src 'attrs\hidden.txt'), 'hidden file')
(Get-Item (Join-Path $src 'attrs\hidden.txt')).Attributes = 'Hidden'

Step '6. 大量文件压力测试'
$stressDir = Join-Path $src 'stress'
$null = New-Item -ItemType Directory -Force -Path $stressDir
for ($i = 0; $i -lt $StressCount; $i++) {
    Write-Bytes (Join-Path $stressDir ("stress_{0:0000}.dat" -f $i)) (($i % 500) + 1) $i
}

$srcFiles = Get-ChildItem $src -Recurse -File -Force
$srcDirs = Get-ChildItem $src -Recurse -Directory -Force
Write-Host "  源文件数: $($srcFiles.Count)，源目录数: $($srcDirs.Count)"

# ============ 全量备份 ============
Step '7. 全量备份（不允许崩溃）'
$encArgs = @()
if ($Encryption -ne 'none') { $encArgs = @('--encrypt', $Encryption, '--password', $Password) }
$ok = Invoke-App backup --source $src --target $bak --type full @encArgs
Check $ok "全量备份成功（encryption=$Encryption）"

Step '8. verify 完整性校验'
$out = & $AppExe verify --backup $bak 2>&1 | Out-String
Check ($LASTEXITCODE -eq 0) 'verify 退出码 0'
Check ($out -match '缺失\s*:\s*0') '缺失 0'
Check ($out -match '损坏\s*:\s*0') '损坏 0'

# ============ 恢复 ============
Step '9. 恢复到独立目录'
$ok = Invoke-App restore --backup $bak --to $restore --overwrite --password $Password
Check $ok '恢复成功'

# ============ 逐文件比对 ============
Step '10. 逐文件 SHA-256 比对（含特殊名/长路径/emoji）'
$restFiles = Get-ChildItem $restore -Recurse -File -Force
Check ($restFiles.Count -eq $srcFiles.Count) "文件数一致（$($srcFiles.Count)）"
$mismatch = 0; $missing = 0
foreach ($f in $srcFiles) {
    $rel = $f.FullName.Substring($src.Length)
    $target = Join-Path $restore $rel
    if (-not (Test-Path -LiteralPath $target)) { $missing++; continue }
    if ((Get-FileHash -LiteralPath $f.FullName).Hash -ne (Get-FileHash -LiteralPath $target).Hash) { $mismatch++ }
}
Check ($missing -eq 0) "无缺失文件（缺失 $missing）"
Check ($mismatch -eq 0) "内容 Hash 全部一致（不一致 $mismatch）"
$emptyDirRestored = Test-Path (Join-Path $restore 'empty_dir')
Check $emptyDirRestored '空目录被保留'
Check ([IO.File]::ReadAllText((Join-Path $restore 'unicode\中文文件.txt')) -eq '中文内容备份') '中文文件名与内容正确'
Check ([IO.File]::ReadAllText((Join-Path $restore 'unicode\emoji😀🎉.txt')) -eq 'emoji content') 'emoji 文件名与内容正确'

Write-Host "`n========================================" -ForegroundColor Yellow
if ($script:failures -eq 0) { Write-Host '健壮性测试全部通过（ALL PASS）' -ForegroundColor Green }
else { Write-Host "有 $script:failures 项失败（FAIL）" -ForegroundColor Red }
Write-Host "加密: $Encryption  压力文件: $StressCount  工作目录: $WorkRoot"
Write-Host "========================================`n" -ForegroundColor Yellow

if ($Cleanup) {
    Get-ChildItem $WorkRoot -Recurse -Force -ErrorAction SilentlyContinue |
        Where-Object { -not $_.PSIsContainer } | ForEach-Object { $_.Attributes = 'Normal' }
    Remove-Item -Recurse -Force $WorkRoot
    Write-Host '已清理测试数据'
}
exit ([int]($script:failures -gt 0))
