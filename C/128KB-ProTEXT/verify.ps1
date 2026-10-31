# ============================================================
# ApolloCalc-CAS · C/128KB-ProTEXT/verify.ps1
# ------------------------------------------------------------
# 【职责 / Responsibility】
#   Pro v2.1.0 的**可重复验收脚本**。一条命令跑完全部硬指标：
#
#       1) 三份产物构建（pro_runner / pro_kernel / degraded）
#       2) .bss 三层账本精确比对（42608 / 42640 / 39408 / 38672）
#       3) 测试套件执行（328 项测试，0 失败）
#       4) B1 / A4 / A6 / A1 / A2 / A3 / A5 / B2 / SP / BD 覆盖度
#       5) 上游 C/128KB 零改动（git status + git diff 双证）
#       6) 负向编译检查（AC_PRO=2 / AC_PRO=0 无 DEGRADED 必须编译失败）
#       7) 关键符号唯一性（拦截层没有制造重复定义）
#
# 【为什么不用 make 而是 PowerShell】
#   .bss 门禁必须解析 `size` 的输出并做数值比较，还要跑二进制读 stdout。
#   make 的 $(if)/$(filter) 能做字符串相等，但做不了「跑程序看退出码」，
#   也没有可读的失败诊断。Makefile 里保留 bss-check / degraded-check 作为
#   快速门禁，本脚本是发版前的完整验收。
#
# 【沙箱说明 / Sandbox note】
#   某些受限环境里 C/128KB-ProTEXT/ 自身只读。本脚本把产物输出到
#   -OutDir（默认 ./.verify-out，被 .gitignore 覆盖），源文件只读。
#
# 用法：
#   pwsh -NoProfile -File C/128KB-ProTEXT/verify.ps1
#   pwsh -NoProfile -File C/128KB-ProTEXT/verify.ps1 -OutDir D:/tmp/pro
# 退出码：0 = 全部通过；1 = 有失败项。
# ============================================================

param(
    [string]$OutDir = ''
)

$ErrorActionPreference = 'Continue'

$proDir  = $PSScriptRoot
$repoRoot = (Resolve-Path (Join-Path $proDir '..\..')).Path
if ($OutDir -eq '') { $OutDir = Join-Path $repoRoot '.verify-out' }
# 【相对 -OutDir 必须立即锚定为绝对路径】本脚本后半段多处 Push-Location
# （第 5/7 节 push $repoRoot、第 1/6 节 push $proDir）；若调用方传入相对
# -OutDir（文档允许），相对目标会在不同 CWD 下解析到**不同**目录：
# New-Item 在调用方 CWD 建目录，而第 7 节的 gcc -o <OutDir>\neg3.o 在
# $repoRoot 下找目录 —— 不存在则 gcc 无法创建输出，exit 1，且 neg1/neg2
# 的「非零 = 预期拒绝」判定会把这个环境性失败**掩盖成通过**（实测：
# harness 以相对 -OutDir 调用时第 7 节第 3 项 FAILED，30/1）。
# 在任何 Push-Location 之前把相对路径锚定到调用方 CWD，全部后续使用
# （make OUTDIR / & $runner / gcc -o）都拿到同一绝对目录，CWD 漂移无关。
if (-not [System.IO.Path]::IsPathRooted($OutDir)) {
    $OutDir = Join-Path (Get-Location -PSProvider FileSystem).ProviderPath $OutDir
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$script:pass = 0
$script:fail = 0

function Verdict([bool]$ok, [string]$label, [string]$detail) {
    if ($ok) {
        $script:pass++
        Write-Host "  [PASS] $label"
    } else {
        $script:fail++
        Write-Host "  [FAIL] $label  ($detail)" -ForegroundColor Red
    }
}

function Get-Bss([string]$exe) {
    $line = (& size $exe) -split "`n" | Select-Object -Last 1
    return [int](($line -split '\s+') | Where-Object { $_ -ne '' })[2]
}

Write-Host ''
Write-Host '============================================================'
Write-Host ' ApolloCalc-CAS · ProTEXT v2.1.0 verification'
Write-Host '============================================================'

# ---------- 1) 构建 ----------
Write-Host ''
Write-Host '--- 1) builds ---'
Push-Location $proDir
& mingw32-make SHELL=cmd.exe OUTDIR=$OutDir pro_runner kernel 2>&1 |
    Where-Object { $_ -match 'error|Error' } | Select-Object -First 5
$runner = Join-Path $OutDir 'pro_runner.exe'
$kernel = Join-Path $OutDir 'pro_kernel.exe'
Verdict (Test-Path $runner) 'pro_runner.exe built' 'missing'
Verdict (Test-Path $kernel) 'pro_kernel.exe built' 'missing'

# ---------- 2) .bss 账本 ----------
Write-Host ''
Write-Host '--- 2) .bss ledger ---'
$bssRunner = Get-Bss $runner
$bssKernel = Get-Bss $kernel
$upExe     = Join-Path $repoRoot 'C\128KB\test_runner.exe'
$bssUp     = if (Test-Path $upExe) { Get-Bss $upExe } else { -1 }
Write-Host "  kernel caliber (pro_kernel.exe) : $bssKernel B   (target 42608)"
Write-Host "  runner caliber (pro_runner.exe) : $bssRunner B   (target 42640)"
Write-Host "  upstream control                : $bssUp B   (target 39408)"
Verdict ($bssKernel -eq 42608) 'kernel .bss == 42608 (38672 + 80 + 3848 + 8, peak view sparse)' "got $bssKernel"
Verdict ($bssRunner -eq 42640) 'runner .bss == 42640 (+32 for the test TU)' "got $bssRunner"
Verdict ($bssUp -eq 39408)     'upstream .bss == 39408 (no drift)' "got $bssUp"
Verdict ($bssKernel -le 114688) 'under the 112 KiB gate' "got $bssKernel"

# ---------- 3) 测试套件 ----------
Write-Host ''
Write-Host '--- 3) test suite ---'
$runOut  = & $runner 2>&1 | Out-String
$runCode = $LASTEXITCODE
$summary = (($runOut -split "`n") | Where-Object { $_ -match 'summary:' } | Select-Object -First 1)
if ($summary) { Write-Host "  $($summary.Trim())" }
Verdict ($runCode -eq 0) 'runner exits 0' "exit $runCode"
Verdict ($runOut -match 'RESULT: PASS') 'RESULT: PASS' 'not PASS'
$failCount = -1
if ($summary -match '(\d+) failed') { $failCount = [int]$Matches[1] }
Verdict ($failCount -eq 0) 'zero failed assertions' "$failCount failed"

# ---------- 4) 每功能用例数 ----------
Write-Host ''
Write-Host '--- 4) per-feature coverage (task book: >= 5 each) ---'
$b1 = ([regex]::Matches($runOut, '\[case B1-\d+\]')).Count
$a4 = ([regex]::Matches($runOut, '\[case A4-\d+\]')).Count
$a6 = ([regex]::Matches($runOut, '\[case A6-\d+\]')).Count
$a5 = ([regex]::Matches($runOut, '\[case A5-\d+\]')).Count
$b2 = ([regex]::Matches($runOut, '\[case B2-\d+\]')).Count
$sp = ([regex]::Matches($runOut, '\[case SP-\d+\]')).Count
$bd = ([regex]::Matches($runOut, '\[case BD-\d+\]')).Count
$gb = ([regex]::Matches($runOut, '\[case GB-\d+\]')).Count
$rat = ([regex]::Matches($runOut, '\[case RAT-\d+\]')).Count
Write-Host "  B1 numeric roots  : $b1 cases"
Write-Host "  A4 LU             : $a4 cases"
Write-Host "  A6 implicit deriv : $a6 cases"
Write-Host "  A5 eigen/deflation: $a5 cases"
Write-Host "  B2 integrate      : $b2 cases"
Write-Host "  SP sparse linsolve: $sp cases"
Write-Host "  BD bigdec (v2.1.0): $bd cases"
Write-Host "  GB groebner (v2.2.0): $gb cases"
Write-Host "  RAT rational (v2.3.0 功能 5): $rat cases"
Verdict ($b1 -ge 5) 'B1 has >= 5 cases' "$b1"
Verdict ($a4 -ge 5) 'A4 has >= 5 cases' "$a4"
Verdict ($a6 -ge 5) 'A6 has >= 5 cases' "$a6"
Verdict ($a5 -ge 5) 'A5 has >= 5 cases' "$a5"
Verdict ($b2 -ge 6) 'B2 has >= 6 cases' "$b2"
Verdict ($sp -ge 8) 'SP has >= 8 cases (v2.1.0 direction 4)' "$sp"
Verdict ($bd -ge 8) 'BD has >= 8 cases (v2.1.0 direction 5)' "$bd"
Verdict ($gb -ge 10) 'GB has >= 10 cases (v2.2.0 direction 6)' "$gb"
Verdict ($rat -ge 5) 'RAT has >= 5 cases (v2.3.0 feature 5)' "$rat"

# ---------- 5) 上游零改动 ----------
Write-Host ''
Write-Host '--- 5) upstream C/128KB untouched ---'
Push-Location $repoRoot
$st = ((& git status --porcelain -- C/128KB) | Out-String).Trim()
$df = ((& git diff --stat -- C/128KB) | Out-String).Trim()
Verdict ($st -eq '') 'git status C/128KB is clean' $st
Verdict ($df -eq '') 'git diff C/128KB is empty' $df
Pop-Location

# ---------- 6) 退化构建 ----------
Write-Host ''
Write-Host '--- 6) degraded build (AC_PRO_DEGRADED=1) ---'
Push-Location $proDir
& mingw32-make SHELL=cmd.exe OUTDIR=$OutDir degraded-check 2>&1 |
    Where-Object { $_ -match 'error|Error' } | Select-Object -First 3
$deg = Join-Path $OutDir 'degraded\pro_kernel.exe'
if (Test-Path $deg) {
    $bssDeg = Get-Bss $deg
    Write-Host "  degraded .bss : $bssDeg B   (target 38672)"
    Verdict ($bssDeg -eq 38672) 'degraded .bss == 38672 (Pro increment compiled out)' "got $bssDeg"
} else {
    Verdict $false 'degraded build' 'missing'
}
Pop-Location

# ---------- 7) 负向编译 ----------
# 每项都把 gcc 的实际退出码与 stderr 摘要写进 verdict detail：像「目录
# 解析错位导致 cannot create output file」这类环境性失败会**自述原因**，
# 而不是一句裸的 failed —— neg1/neg2 尤其重要，它们的判定是「非零 =
# 通过」，任何非编译语义的非零（如输出路径不可写）都会被误判为拒绝。
Write-Host ''
Write-Host '--- 7) negative compile checks ---'
Push-Location $repoRoot
$parser = Join-Path $proDir 'src\ac_pro_parser.c'
$negOut = (& gcc -std=c99 -DAC_PRO=2 -c $parser -o (Join-Path $OutDir 'neg1.o') 2>&1 | Out-String).Trim()
$negRc  = $LASTEXITCODE
Verdict ($negRc -ne 0) 'AC_PRO=2 rejected at compile time' "exit $negRc unexpectedly compiled; output: $negOut"
$negOut = (& gcc -std=c99 -DAC_PRO=0 -c $parser -o (Join-Path $OutDir 'neg2.o') 2>&1 | Out-String).Trim()
$negRc  = $LASTEXITCODE
Verdict ($negRc -ne 0) 'AC_PRO=0 without DEGRADED rejected' "exit $negRc unexpectedly compiled; output: $negOut"
$negOut = (& gcc -std=c99 -Wall -Wextra -Werror -DAC_PRO=0 -DAC_PRO_DEGRADED=1 -c $parser -o (Join-Path $OutDir 'neg3.o') 2>&1 | Out-String).Trim()
$negRc  = $LASTEXITCODE
Verdict ($negRc -eq 0) 'AC_PRO=0 + DEGRADED compiles clean' "exit $negRc; output: $negOut"
Pop-Location

# ---------- 8) 符号唯一性 ----------
Write-Host ''
Write-Host '--- 8) symbol uniqueness in pro_kernel.exe ---'
$nmOut = (& nm $kernel) | Out-String
foreach ($s in @('ac_guard_degree', 'ac_pro_upstream_guard_degree', 'ac_pro_numroots',
                 'ac_mat_lu', 'ac_pro_implicit_derivative', 'g_ac_pro_storage',
                 'g_ac_scratch', 'g_sym_nodes')) {
    $c = ([regex]::Matches($nmOut, "(?m)\s+[A-Za-z]\s+$([regex]::Escape($s))\s*$")).Count
    Verdict ($c -eq 1) "symbol $s appears exactly once" "count = $c"
}

# ---------- 汇总 ----------
Write-Host ''
Write-Host '============================================================'
if ($script:fail -eq 0) {
    Write-Host " RESULT: $($script:pass) passed, 0 failed" -ForegroundColor Green
} else {
    Write-Host " RESULT: $($script:pass) passed, $($script:fail) failed" -ForegroundColor Red
}
Write-Host '============================================================'
Write-Host ''

if ($script:fail -ne 0) { exit 1 }
exit 0
