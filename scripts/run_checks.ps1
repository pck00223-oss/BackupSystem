# One-shot code robustness check
# Usage: from project root:  powershell -ExecutionPolicy Bypass -File scripts\run_checks.ps1
$ErrorActionPreference = "Continue"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

function Section($name) { Write-Host "`n========== $name ==========" -ForegroundColor Cyan }

# 1. Strict warnings build
Section "1/3 Strict warnings build (build-strict)"
cmake -S . -B build-strict -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSTRICT_WARNINGS=ON 2>&1 | Out-Null
$strictOut = cmake --build build-strict 2>&1
$strictWarn = ($strictOut | Select-String -Pattern "warning:").Count
$strictErr  = ($strictOut | Select-String -Pattern "error:").Count
$strictOut | Select-String -Pattern "warning:|error:" | ForEach-Object { $_.Line }
Write-Host "Strict build: $strictWarn warning(s), $strictErr error(s)" -ForegroundColor $(if($strictErr -gt 0){'Red'}else{'Green'})

# 2. cppcheck static analysis
Section "2/3 cppcheck static analysis"
$cppOut = cppcheck --enable=warning,style,performance,portability --inconclusive --std=c++17 --suppress=missingIncludeSystem --suppress=unusedFunction -I include src tests 2>&1
$cppOut | ForEach-Object { $_.ToString() }
$cppIssues = ($cppOut | Select-String -Pattern ":\d+:\d+:\s+(error|warning|style|performance|portability):").Count
Write-Host "cppcheck: $cppIssues issue(s)" -ForegroundColor $(if($cppIssues -gt 0){'Yellow'}else{'Green'})

# 3. Hardened build (libstdc++ debug + stack protector + trapv) and run tests
Section "3/3 Hardened build + tests (build-hard)"
cmake -S . -B build-hard -G Ninja -DCMAKE_BUILD_TYPE=Debug -DHARDENING=ON 2>&1 | Out-Null
$hardOut = cmake --build build-hard 2>&1
$hardErr = ($hardOut | Select-String -Pattern "error:").Count
if ($hardErr -gt 0) {
    $hardOut | Select-String -Pattern "error:" | ForEach-Object { $_.Line }
    Write-Host "Hardened build FAILED with $hardErr error(s)" -ForegroundColor Red
} else {
    $testOut = & ".\build-hard\backup_tests.exe" 2>&1
    $testOut | Select-String -Pattern "total|\[FAIL\]|abort|terminate" | ForEach-Object { $_.Line }
    $testFail = ($testOut | Select-String -Pattern "\[FAIL\]|abort|terminate").Count
    Write-Host "Hardened tests: $(if($testFail -eq 0){'PASS'}else{'ISSUES FOUND'})" -ForegroundColor $(if($testFail -eq 0){'Green'}else{'Red'})
}

Section "Done"
Write-Host "Strict build dir:  build-strict/"
Write-Host "Hardened build:    build-hard/"
Write-Host "Regular build:     build/"
