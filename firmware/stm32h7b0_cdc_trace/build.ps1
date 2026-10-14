# STM32H7B0 + CherryUSB CDC ACM + etm-trace 钩子 —— GCC 构建脚本
#
# 用法：
#   pwsh -NoProfile -File .\build.ps1                    # CPU 280MHz / TRACEPORTCK 140MHz / 物理 TRACECLK 70MHz
#   pwsh -NoProfile -File .\build.ps1 -CoreMHz 140       # 低频档（PLL P=4 -> 140MHz）
#   pwsh -NoProfile -File .\build.ps1 -TraceR 8          # TRACEPORTCK 改 /8
#
# 产物：build\<档位>\fw.*、fw-<档位>.*；build\fw.* / fw.* 保留最近一次构建副本。
# 只编译，不烧录；固件**不配置 ETM**，trace 由外部 SWD 工具配置。
param(
    [ValidateSet('280', '140', '70')][string]$CoreMHz = '280',
    [ValidateSet(0, 4, 5, 6, 8, 16)][int]$TraceR = 0,
    [ValidateSet('External', 'Internal')][string]$UsbSupply = 'External',
    [string]$Gcc
)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot '..\tools\BuildTools.psm1') -Force

$h7Root = $PSScriptRoot
if (-not $Gcc) { $Gcc = (Get-Command arm-none-eabi-gcc).Source }
$h7Bin = Split-Path -Parent $Gcc
$h7Sdk = Join-Path $h7Root 'sdk'
$h7Profile = $CoreMHz
if ($TraceR -ne 0) { $h7Profile = "$CoreMHz-r$TraceR" }
if ($UsbSupply -eq 'Internal') { $h7Profile += '-usb-internal' }
$h7Build = Join-Path $h7Root "build\$h7Profile"
New-Item -ItemType Directory -Path $h7Build -Force | Out-Null

# PLL1 R 决定 TRACEPORTCK：VCO 560MHz
$h7P = 2
$h7R = 4
if ($CoreMHz -eq '140') { $h7P = 4 }
if ($CoreMHz -eq '70') { $h7P = 8; $h7R = 8 }
if ($TraceR -ne 0) { $h7R = $TraceR }
$h7UsbReg = [int]($UsbSupply -eq 'Internal')

$h7Flags = @(
    '-mcpu=cortex-m7', '-mthumb', '-mfpu=fpv5-d16', '-mfloat-abi=hard',
    '-Og', '-g3', '-gdwarf-4',
    '-fno-inline', '-fno-optimize-sibling-calls',
    '-fno-if-conversion', '-fno-if-conversion2',
    '-ffunction-sections', '-fdata-sections', '-fno-common',
    '-Wall', '-Wextra', '-Wno-unused-parameter', '-Wno-unused-function',
    '-DUSE_HAL_DRIVER', '-DSTM32H7B0xx', '-DHSE_VALUE=25000000',
    "-DH7_CDC_PLL_P=$h7P", "-DH7_CDC_PLL_R=$h7R",
    "-DH7_CDC_USB_INTERNAL_REGULATOR=$h7UsbReg",
    # SEGGER RTT 控制块固定到 .rtt_cb（链接脚本把它放在 DTCM 0x20002000）
    '-DRTT_CB_ATTR=__attribute__((section(".rtt_cb"),aligned(4)))',
    "-I$h7Root\src",
    "-I$h7Sdk\Core\Inc",
    "-I$h7Sdk\RTT_TRACE_LOG",
    "-I$h7Root\cherryusb\config",
    "-I$h7Root\cherryusb\core",
    "-I$h7Root\cherryusb\class\cdc",
    "-I$h7Root\cherryusb\common",
    "-I$h7Root\cherryusb\port\dwc2",
    "-I$h7Sdk\Drivers\STM32H7xx_HAL_Driver\Inc",
    "-I$h7Sdk\Drivers\STM32H7xx_HAL_Driver\Inc\Legacy",
    "-I$h7Sdk\Drivers\CMSIS\Device\ST\STM32H7xx\Include",
    "-I$h7Sdk\Drivers\CMSIS\Include"
)

# ---------------- 源文件清单 ----------------
$h7Sources = @(
    "$h7Root\src\main.c",
    "$h7Root\src\cdc_console.c",
    "$h7Root\src\runtime.c",
    "$h7Root\src\trace_hooks.c",
    "$h7Root\src\boot_probe.c",
    "$h7Root\src\usb_dc_low_level.c",
    "$h7Sdk\Core\Src\system_stm32h7xx.c",
    "$h7Sdk\RTT_TRACE_LOG\SEGGER_RTT.c",
    "$h7Sdk\RTT_TRACE_LOG\SEGGER_RTT_printf.c",
    "$h7Root\cherryusb\core\usbd_core.c",
    "$h7Root\cherryusb\class\cdc\usbd_cdc_acm.c",
    "$h7Root\cherryusb\port\dwc2\usb_dc_dwc2.c",
    "$h7Root\cherryusb\port\dwc2\usb_glue_st_h7b0.c"
)

# HAL：只挑这个 demo 用得上的（PCD 是必需项 —— CherryUSB 的 glue 走 HAL_PCD_MspInit）
foreach ($h7Hal in @('hal', 'hal_cortex', 'hal_rcc', 'hal_rcc_ex', 'hal_pwr', 'hal_pwr_ex',
                     'hal_flash', 'hal_flash_ex', 'hal_gpio', 'hal_dma', 'hal_dma_ex',
                     'hal_exti', 'hal_pcd', 'hal_pcd_ex', 'hal_hsem', 'hal_mdma')) {
    $h7Sources += Join-Path $h7Sdk "Drivers\STM32H7xx_HAL_Driver\Src\stm32h7xx_$h7Hal.c"
}
# ll_usb.c 提供 USB_ReadInterrupts/USB_SetRxFiFo 等 HAL PCD 依赖的底层函数
$h7Sources += Join-Path $h7Sdk 'Drivers\STM32H7xx_HAL_Driver\Src\stm32h7xx_ll_usb.c'

$h7Startup = Join-Path $h7Sdk 'Drivers\CMSIS\Device\ST\STM32H7xx\Source\Templates\gcc\startup_stm32h7b0xx.s'

# ---------------- 编译 ----------------
$h7Objects = @()
foreach ($h7Src in $h7Sources) {
    if (-not (Test-Path $h7Src)) { throw "源文件不存在：$h7Src" }
    $h7Obj = Join-Path $h7Build ([IO.Path]::GetFileNameWithoutExtension($h7Src) + '.o')
    $r = Invoke-Tool -Exe $Gcc -Args (@($h7Flags) + @('-c', $h7Src, '-o', $h7Obj))
    if ($r.ExitCode -ne 0) { throw "编译失败：$h7Src" }
    $h7Objects += $h7Obj
}
$h7StartupObj = Join-Path $h7Build 'startup.o'
$r = Invoke-Tool -Exe $Gcc -Args (@($h7Flags) + @('-c', $h7Startup, '-o', $h7StartupObj))
if ($r.ExitCode -ne 0) { throw "编译失败：$h7Startup" }
$h7Objects += $h7StartupObj

# ---------------- 链接 ----------------
# 用 -nostartfiles：H7 的 ST 启动文件自己准备栈与向量，不需要 crt0；
# src/runtime.c 提供 __libc_init_array 调用的真实 _init()。
$h7Elf = Join-Path $h7Build 'fw.elf'
$r = Invoke-Tool -Exe $Gcc -Args (@($h7Flags) + @($h7Objects) + @("-T$h7Root\ld\stm32h7b0.ld", '-nostartfiles',
    '-specs=nano.specs', '-specs=nosys.specs', '-Wl,--gc-sections',
    "-Wl,-Map=$h7Build\fw.map", '-o', $h7Elf))
if ($r.ExitCode -ne 0) { throw '链接失败' }

foreach ($h7Fmt in @(@('binary', 'bin'), @('ihex', 'hex'))) {
    $r = Invoke-Tool -Exe "$h7Bin\arm-none-eabi-objcopy.exe" -Args @('-O', $h7Fmt[0], $h7Elf, "$h7Build\fw.$($h7Fmt[1])")
    if ($r.ExitCode -ne 0) { throw '转换失败' }
}

Write-Output (Invoke-Tool -Exe "$h7Bin\arm-none-eabi-size.exe" -Args @($h7Elf)).Stdout
(Invoke-Tool -Exe "$h7Bin\arm-none-eabi-nm.exe" -Args @('-n', '-S', $h7Elf)).Stdout | Set-Content -LiteralPath "$h7Build\fw.symbols.txt" -Encoding utf8
(Invoke-Tool -Exe "$h7Bin\arm-none-eabi-objdump.exe" -Args @('-d', '-S', $h7Elf)).Stdout | Set-Content -LiteralPath "$h7Build\fw.disasm.txt" -Encoding utf8
foreach ($h7Ext in @('elf', 'bin', 'hex')) {
    Copy-Item -LiteralPath "$h7Build\fw.$h7Ext" -Destination "$h7Root\fw-$h7Profile.$h7Ext" -Force
    Copy-Item -LiteralPath "$h7Build\fw.$h7Ext" -Destination "$h7Root\fw.$h7Ext" -Force
    Copy-Item -LiteralPath "$h7Build\fw.$h7Ext" -Destination "$h7Root\build\fw.$h7Ext" -Force
}
foreach ($h7Report in @('map', 'symbols.txt', 'disasm.txt')) {
    Copy-Item -LiteralPath "$h7Build\fw.$h7Report" -Destination "$h7Root\build\fw.$h7Report" -Force
}

Write-Output "构建完成：CPU ${CoreMHz}MHz / TRACEPORTCK $(560 / $h7R)MHz / 物理 TRACECLK $(280 / $h7R)MHz / USB ${UsbSupply} -> $h7Elf"
Write-Output "固件不开启 ETM；trace 由外部 SWD 工具配置。关键符号："
Select-String -Path "$h7Build\fw.symbols.txt" -Pattern ' g_trace$| _SEGGER_RTT$| g_boot_stage$| OTG_HS_IRQHandler$| trace_hook_entry$| _init$' | ForEach-Object { $_.Line }
