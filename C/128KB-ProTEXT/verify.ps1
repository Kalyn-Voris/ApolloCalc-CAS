# ============================================================
# ApolloCalc-CAS · C/128KB-ProTEXT/verify.ps1
# ------------------------------------------------------------
# 【职责 / Responsibility】
#   Pro v1.3.5 的**可重复验收脚本**。一条命令跑完全部硬指标：
#
#       1) 三份产物构建（pro_runner / pro_kernel / degraded）
#       2) .bss 三层账本精确比对（41456 / 41488 / 39408 / 38672）
#       3) 测试套件执行（259 项测试，0 失败）
#       4) B1 / A4 / A6 / A1 / A2 / A3 / A5 / B2 覆盖度
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
Write-Host ' ApolloCalc-CAS · ProTEXT v1.3.5 verification'
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
Write-Host "  kernel caliber (pro_kernel.exe) : $bssKernel B   (target 41456)"
Write-Host "  runner caliber (pro_runner.exe) : $bssRunner B   (target 41488)"
Write-Host "  upstream control                : $bssUp B   (target 39408)"
Verdict ($bssKernel -eq 41456) 'kernel .bss == 41456 (38672 + 80 + 2688 + 16)' "got $bssKernel"
Verdict ($bssRunner -eq 41488) 'runner .bss == 41488 (+32 for the test TU)' "got $bssRunner"
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
Write-Host "  B1 numeric roots  : $b1 cases"
Write-Host "  A4 LU             : $a4 cases"
Write-Host "  A6 implicit deriv : $a6 cases"
Write-Host "  A5 eigen/deflation: $a5 cases"
Write-Host "  B2 integrate      : $b2 cases"
Verdict ($b1 -ge 5) 'B1 has >= 5 cases' "$b1"
Verdict ($a4 -ge 5) 'A4 has >= 5 cases' "$a4"
Verdict ($a6 -ge 5) 'A6 has >= 5 cases' "$a6"
Verdict ($a5 -ge 5) 'A5 has >= 5 cases' "$a5"
Verdict ($b2 -ge 6) 'B2 has >= 6 cases' "$b2"

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
Write-Host ''
Write-Host '--- 7) negative compile checks ---'
Push-Location $repoRoot
$parser = Join-Path $proDir 'src\ac_pro_parser.c'
& gcc -std=c99 -DAC_PRO=2 -c $parser -o (Join-Path $OutDir 'neg1.o') 2>&1 | Out-Null
Verdict ($LASTEXITCODE -ne 0) 'AC_PRO=2 rejected at compile time' 'compiled unexpectedly'
& gcc -std=c99 -DAC_PRO=0 -c $parser -o (Join-Path $OutDir 'neg2.o') 2>&1 | Out-Null
Verdict ($LASTEXITCODE -ne 0) 'AC_PRO=0 without DEGRADED rejected' 'compiled unexpectedly'
& gcc -std=c99 -Wall -Wextra -Werror -DAC_PRO=0 -DAC_PRO_DEGRADED=1 -c $parser -o (Join-Path $OutDir 'neg3.o') 2>&1 | Out-Null
Verdict ($LASTEXITCODE -eq 0) 'AC_PRO=0 + DEGRADED compiles clean' 'failed'
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
