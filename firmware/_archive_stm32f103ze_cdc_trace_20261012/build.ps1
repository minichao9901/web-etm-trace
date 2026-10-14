# STM32F103 + CherryUSB CDC ACM + etm-trace 钩子 —— GCC 构建脚本
#
# 用法：
#   pwsh -NoProfile -File .\build.ps1                 # 默认 -Chip cb（在线板 F103CB，64K/20K）
#   pwsh -NoProfile -File .\build.ps1 -Chip ze        # 目标板 F103ZE，512K/64K
#   pwsh -NoProfile -File .\build.ps1 -Chip cb -HseMHz 8
#
# 产物：build\<chip>\fw.elf / fw.bin / fw.hex（根目录也会放一份同名副本）
# 只编译，不烧录；烧录用 flash.ps1（pyocd + CMSIS-DAP）。
param(
    [ValidateSet('cb', 'ze')][string]$Chip = 'cb',
    [int]$HseMHz = 8,
    [string]$Gcc
)
$ErrorActionPreference = 'Stop'

$f1Root   = $PSScriptRoot
if (-not $Gcc) { $Gcc = (Get-Command arm-none-eabi-gcc).Source }
$f1Bin    = Split-Path -Parent $Gcc
$f1Sdk    = Join-Path $f1Root 'sdk'
$f1Build  = Join-Path $f1Root "build\$Chip"
New-Item -ItemType Directory -Path $f1Build -Force | Out-Null

# 芯片相关参数
switch ($Chip) {
    'cb' { $f1Def = 'STM32F103xB'; $f1Startup = 'startup_stm32f103xb.s'; $f1Ld = 'stm32f103cb.ld' }
    'ze' { $f1Def = 'STM32F103xE'; $f1Startup = 'startup_stm32f103xe.s'; $f1Ld = 'stm32f103ze.ld' }
}
$f1Hse = $HseMHz * 1000000

$f1Flags = @(
    '-mcpu=cortex-m3', '-mthumb',
    '-Og', '-g3', '-gdwarf-4',
    '-fno-inline', '-fno-optimize-sibling-calls',
    '-ffunction-sections', '-fdata-sections', '-fno-common',
    '-Wall', '-Wextra', '-Wno-unused-parameter', '-Wno-unused-function',
    "-D$f1Def", "-DHSE_VALUE=$f1Hse", '-DUSE_HAL_DRIVER',
    # SEGGER RTT 控制块固定到 .rtt_cb（链接脚本把它放在 RAM 末尾的高地址，见 ld/*.ld）
    '-DRTT_CB_ATTR=__attribute__((section(".rtt_cb"),aligned(4)))',
    "-I$f1Root\src",
    "-I$f1Root\sdk",
    "-I$f1Root\segger_rtt",
    "-I$f1Root\cherryusb\config",
    "-I$f1Root\cherryusb\core",
    "-I$f1Root\cherryusb\class\cdc",
    "-I$f1Root\cherryusb\common",
    "-I$f1Root\cherryusb\port\fsdev",
    "-I$f1Sdk\CMSIS\Core",
    "-I$f1Sdk\CMSIS\Device\ST\STM32F1xx\Include",
    "-I$f1Sdk\STM32F1xx_HAL_Driver\Inc",
    "-I$f1Sdk\STM32F1xx_HAL_Driver\Inc\Legacy"
)

# ---------------- 源文件清单 ----------------
$f1Sources = @(
    "$f1Root\src\main.c",
    "$f1Root\src\system_clock.c",
    "$f1Root\src\boot_probe.c",
    "$f1Root\src\bcdr_probe.c",
    "$f1Root\src\trace_hooks.c",
    "$f1Root\src\usb_dc_low_level.c",
    "$f1Root\src\stm32f1xx_it.c",
    "$f1Root\src\syscalls.c",
    "$f1Root\segger_rtt\SEGGER_RTT.c",
    "$f1Root\segger_rtt\SEGGER_RTT_printf.c",
    "$f1Root\cherryusb\core\usbd_core.c",
    "$f1Root\cherryusb\class\cdc\usbd_cdc_acm.c",
    "$f1Root\cherryusb\port\fsdev\usb_dc_fsdev.c",
    "$f1Sdk\CMSIS\Device\ST\STM32F1xx\Source\Templates\system_stm32f1xx.c"
)

# HAL 只挑用得上的模块（注意：**不包含** stm32f1xx_hal_pcd.c，USB 状态机是 CherryUSB 的）
foreach ($f1Hal in @('hal', 'hal_cortex', 'hal_rcc', 'hal_rcc_ex', 'hal_gpio',
                     'hal_gpio_ex', 'hal_flash', 'hal_flash_ex', 'hal_dma',
                     'hal_exti', 'hal_pwr')) {
    $f1Sources += Join-Path $f1Sdk "STM32F1xx_HAL_Driver\Src\stm32f1xx_$f1Hal.c"
}

# 启动文件（汇编，单独编）
$f1StartupPath = Join-Path $f1Sdk "CMSIS\Device\ST\STM32F1xx\Source\Templates\gcc\$f1Startup"

# ---------------- 编译 ----------------
# 启动文件（汇编，单独编）
$f1StartupPath = Join-Path $f1Sdk "CMSIS\Device\ST\STM32F1xx\Source\Templates\gcc\$f1Startup"

$f1Objects = @()
foreach ($f1Src in $f1Sources) {
    if (-not (Test-Path $f1Src)) { throw "源文件不存在：$f1Src" }
    $f1Obj = Join-Path $f1Build ([IO.Path]::GetFileNameWithoutExtension($f1Src) + '.o')
    & $Gcc @f1Flags -c $f1Src -o $f1Obj
    if ($LASTEXITCODE -ne 0) { throw "编译失败：$f1Src" }
    $f1Objects += $f1Obj
}
$f1StartupObj = Join-Path $f1Build 'startup.o'
& $Gcc @f1Flags -c $f1StartupPath -o $f1StartupObj
if ($LASTEXITCODE -ne 0) { throw "编译失败：$f1StartupPath" }

# ⚠️⚠️ 顺序很关键：**startup.o 必须排在所有目标文件最前面**！
#   ST 的启动文件对每个中断都写了
#       .weak  XXX_IRQHandler
#       .thumb_set XXX_IRQHandler, Default_Handler
#   而 `.thumb_set` 会在**该目标文件内部把符号绑定死**，使它不再去后面的目标文件
#   寻找强定义。如果 startup.o 排在最后，链接器会先采纳我们实现的强符号、
#   随后被启动文件的 .thumb_set 覆盖 —— 结果就是向量表里全指向 Default_Handler 死循环。
#   症状极具迷惑性：SysTick 正常（因为排序在覆盖点之前）、而 USB_HP/USB_LP 两个向量
#   指向 Default_Handler，表现为 USB 完全不响应、Windows 报"设备描述符请求失败"。
$f1Objects = @($f1StartupObj) + $f1Objects

# ---------------- 链接 ----------------
# 注意 1：这里**不能**加 -nostartfiles —— ST 的 F1 启动文件里有 `bl __libc_init_array`，
#         而 _init 是由 crti.o 提供的，去掉标准启动文件就会 undefined reference to `_init'。
#
# 注意 2（最关键的一条坑）：ST 的 startup_stm32f103xb.s 对每个中断都写了
#         .weak  XXX_IRQHandler
#         .thumb_set XXX_IRQHandler, Default_Handler
#      这个组合会**在 startup.o 内部把符号绑定死** —— 即使我们在别的文件里提供强定义，
#      向量表依然指向 Default_Handler（实测 vector[19]/[20] 都是 Default_Handler 地址，
#      而 nm 里我们的强符号明明存在；把 .weak 注释掉就立刻变成可解析的 U）。
#      症状：USB 中断永远进不来 → 枚举卡死 → Windows 报「设备描述符请求失败 Code 43」。
#      解决：我们把自己的处理函数改名（f1_usb_lp_handler / f1_usb_hp_handler），
#      再用 --defsym 把向量表里的标准名字重定向到它。地址无关，也不用改 ST 的文件。
$f1Elf = Join-Path $f1Build 'fw.elf'
& $Gcc @f1Flags @f1Objects "-T$f1Root\ld\$f1Ld" `
    '-Wl,--defsym=USB_LP_CAN1_RX0_IRQHandler=f1_usb_lp_handler' `
    '-Wl,--defsym=USB_HP_CAN1_TX_IRQHandler=f1_usb_hp_handler' `
    '-specs=nano.specs' '-specs=nosys.specs' '-Wl,--gc-sections' `
    "-Wl,-Map=$f1Build\fw.map" -o $f1Elf
if ($LASTEXITCODE -ne 0) { throw '链接失败' }

foreach ($f1Fmt in @(@('binary', 'bin'), @('ihex', 'hex'))) {
    & "$f1Bin\arm-none-eabi-objcopy.exe" -O $f1Fmt[0] $f1Elf "$f1Build\fw.$($f1Fmt[1])"
    if ($LASTEXITCODE -ne 0) { throw ' 转换失败' }
}

& "$f1Bin\arm-none-eabi-size.exe" $f1Elf
& "$f1Bin\arm-none-eabi-nm.exe" -n -S $f1Elf | Set-Content -LiteralPath "$f1Build\fw.symbols.txt" -Encoding utf8
& "$f1Bin\arm-none-eabi-objdump.exe" -d -S $f1Elf | Set-Content -LiteralPath "$f1Build\fw.disasm.txt" -Encoding utf8

foreach ($f1Ext in @('elf', 'bin', 'hex')) {
    Copy-Item -LiteralPath "$f1Build\fw.$f1Ext" -Destination "$f1Root\fw-$Chip.$f1Ext" -Force
}

Write-Output "构建完成：chip=$Chip ($f1Def) HSE=${HseMHz}MHz -> $f1Elf"
Write-Output "诊断变量地址："
Select-String -Path "$f1Build\fw.symbols.txt" -Pattern ' g_trace$| _SEGGER_RTT$' | ForEach-Object { $_.Line }
