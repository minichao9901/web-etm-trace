# STM32F103 CDC ACM 固件烧录脚本（pyocd + akaLinkPro CMSIS-DAP）
#
# 用法：
#   pwsh -NoProfile -File .\flash.ps1                 # 烧 fw-cb.bin（在线板 F103CB/R8）
#   pwsh -NoProfile -File .\flash.ps1 -Chip ze
#   pwsh -NoProfile -File .\flash.ps1 -Target stm32f103cb
#
# 依赖：pyocd（已装）+ 一个 CMSIS-DAP 探针（当前用 akaLinkPro）。
# 注意：只写 Flash 并复位，不做芯片擦除之外的任何破坏性操作。
param(
    [ValidateSet('cb', 'ze')][string]$Chip = 'cb',
    [string]$Target = 'stm32f103cb'
)
$ErrorActionPreference = 'Stop'

$f1Root = $PSScriptRoot
$f1Bin  = Join-Path $f1Root "fw-$Chip.bin"
if (-not (Test-Path $f1Bin)) { throw "找不到固件：$f1Bin（先跑 build.ps1 -Chip $Chip）" }

$f1Pyocd = (Get-Command pyocd).Source
Write-Output "探针："; & $f1Pyocd list

Write-Output "烧录 $f1Bin -> $Target @0x08000000"
& $f1Pyocd flash -t $Target --base-address 0x08000000 $f1Bin
if ($LASTEXITCODE -ne 0) { throw "烧录失败（exit $LASTEXITCODE）" }

Write-Output "复位并检查 PC/SP："
& $f1Pyocd cmd -t $Target -c "reset" -c "reg pc" -c "reg sp"
