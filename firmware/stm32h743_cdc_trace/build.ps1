# STM32H743 (阿波罗, HSE 25MHz) + CherryUSB CDC ACM + etm-trace 钩子 —— GCC 构建脚本
#
# 用法：
#   pwsh -NoProfile -File .\build.ps1                    # CPU 480MHz / TRACEPORTCK 480MHz(VCO 960/R=2)
#   pwsh -NoProfile -File .\build.ps1 -TraceR 4          # TRACEPORTCK 改 240MHz
#   （本工程固定 480MHz —— 阿波罗 H743 + 25MHz 晶振，PLLN=192；要降频改 src/main.c）
#
# 产物：build\fw.elf / fw.bin / fw.hex，并在根目录放一份副本。
# 只编译，不烧录；固件**不配置 ETM**，trace 由外部 SWD 工具配置。
param(
    [ValidateSet('480')][string]$CoreMHz = '480',
    [ValidateSet(0, 4, 5, 6, 8, 16)][int]$TraceR = 0,
    [string]$Gcc
)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot '..\tools\BuildTools.psm1') -Force

$h7Root = $PSScriptRoot
if (-not $Gcc) { $Gcc = (Get-Command arm-none-eabi-gcc).Source }
$h7Bin = Split-Path -Parent $Gcc
$h7Sdk = Join-Path $h7Root 'sdk'
$h7Build = Join-Path $h7Root 'build'
New-Item -ItemType Directory -Path $h7Build -Force | Out-Null

# VCO = 960MHz（25MHz/5*192）；P=2 -> SYSCLK 480MHz；R 决定 TRACEPORTCK
$h7P = 2
$h7R = 2
if ($TraceR -ne 0) { $h7R = $TraceR }

$h7Flags = @(
    '-mcpu=cortex-m7', '-mthumb', '-mfpu=fpv5-d16', '-mfloat-abi=hard',
    '-Og', '-g3', '-gdwarf-4',
    '-fno-inline', '-fno-optimize-sibling-calls',
    '-fno-if-conversion', '-fno-if-conversion2',
    '-ffunction-sections', '-fdata-sections', '-fno-common',
    '-Wall', '-Wextra', '-Wno-unused-parameter', '-Wno-unused-function',
    '-DUSE_HAL_DRIVER', '-DSTM32H743xx', '-DHSE_VALUE=25000000',
    "-DH7_CDC_PLL_P=$h7P", "-DH7_CDC_PLL_R=$h7R",
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

$h7Startup = Join-Path $h7Sdk 'Drivers\CMSIS\Device\ST\STM32H7xx\Source\Templates\gcc\startup_stm32h743xx.s'

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
# 同时链接脚本里 PROVIDE(_init=0) 兜住 __libc_init_array 对 _init 的引用。
$h7Elf = Join-Path $h7Build 'fw.elf'
$r = Invoke-Tool -Exe $Gcc -Args (@($h7Flags) + @($h7Objects) + @("-T$h7Root\ld\stm32h743.ld", '-nostartfiles',
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
    Copy-Item -LiteralPath "$h7Build\fw.$h7Ext" -Destination "$h7Root\fw.$h7Ext" -Force
}

Write-Output "构建完成：STM32H743 CPU ${CoreMHz}MHz / TRACEPORTCK $(960 / $h7R)MHz -> $h7Elf"
Write-Output "固件不开启 ETM；trace 由外部 SWD 工具配置。关键符号："
Select-String -Path "$h7Build\fw.symbols.txt" -Pattern ' g_trace$| _SEGGER_RTT$| g_boot_stage$| OTG_FS_IRQHandler' | ForEach-Object { $_.Line }
