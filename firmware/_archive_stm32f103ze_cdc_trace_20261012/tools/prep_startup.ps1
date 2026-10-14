#!/usr/bin/env pwsh
# prep_startup.ps1 —— 去掉 ST F1 启动文件里 USB 两条中断的弱别名
#
# 为什么必须做这一步（实测结论，别删）：
#   ST 的 startup_stm32f103xb.s / startup_stm32f103xe.s 对**每个**中断都写了
#       .weak  USB_LP_CAN1_RX0_IRQHandler
#       .thumb_set USB_LP_CAN1_RX0_IRQHandler, Default_Handler
#
#   实测行为（arm-none-eabi-gcc 10.3.1 + GNU ld）：
#     * 两条都在  -> startup.o 里该符号是 W（弱定义），向量表指向 Default_Handler；
#                   我们在别的文件里的强定义**不起作用**。
#     * 只留 .weak -> startup.o 里是 U（未定义弱符号）；向量表**仍然**指向 Default_Handler。
#     * 两条都去掉 -> startup.o 里是 U（普通未定义），向量表正常解析到我们的强定义 ✓
#   链接顺序调整、`--defsym=名字=名字`、`--defsym=名字=新名字` 都试过，**均无效**。
#
#   后果：USB 中断永远进不来 → STM32F1 枚举卡死 → Windows 报
#         「未知 USB 设备(设备描述符请求失败) Code 43」。
#
# 本脚本对启动文件做的唯一改动：把这两条中断的 .weak / .thumb_set 注释掉。
# 幂等：重复运行不会二次修改（已注释就跳过）。原文件备份为 *.s.orig。
param(
    [string[]]$Startup = @(
        "$PSScriptRoot\sdk\CMSIS\Device\ST\STM32F1xx\Source\Templates\gcc\startup_stm32f103xb.s",
        "$PSScriptRoot\sdk\CMSIS\Device\ST\STM32F1xx\Source\Templates\gcc\startup_stm32f103xe.s"
    )
)
$ErrorActionPreference = 'Stop'
$targets = @('USB_HP_CAN1_TX_IRQHandler', 'USB_LP_CAN1_RX0_IRQHandler')

foreach ($f in $Startup) {
    if (-not (Test-Path $f)) { Write-Warning "跳过（不存在）：$f"; continue }
    $bak = "$f.orig"
    if (-not (Test-Path $bak)) { Copy-Item $f $bak; Write-Host "已备份原文件 -> $bak" }

    $text = Get-Content $f -Raw
    $changed = 0
    foreach ($sym in $targets) {
        # .weak SYM
        $pat1 = "(?m)^(\s*)\.weak\s+$([regex]::Escape($sym))\s*$"
        if ([regex]::IsMatch($text, $pat1)) {
            $text = [regex]::Replace($text, $pat1, "`$1@ [prep_startup] .weak $sym 已注释（见脚本注释）")
            $changed++
        }
        # .thumb_set SYM,Default_Handler
        $pat2 = "(?m)^(\s*)\.thumb_set\s+$([regex]::Escape($sym))\s*,\s*Default_Handler\s*$"
        if ([regex]::IsMatch($text, $pat2)) {
            $text = [regex]::Replace($text, $pat2, "`$1@ [prep_startup] .thumb_set $sym -> Default_Handler 已注释")
            $changed++
        }
    }
    if ($changed -gt 0) {
        Set-Content -Path $f -Value $text -NoNewline
        Write-Host "已修改 $([IO.Path]::GetFileName($f))：注释掉 $changed 条弱别名"
    } else {
        Write-Host "无需修改（可能已处理过）：$([IO.Path]::GetFileName($f))"
    }
}

Write-Host ""
Write-Host "验证：重新编译 startup 后这两个符号应为 U（未定义），而不是 W（弱定义）："
$gcc = (Get-Command arm-none-eabi-gcc).Source
& $gcc -mcpu=cortex-m3 -mthumb -c $Startup[0] -o "$env:TEMP\_prep_check.o" 2>&1 | Out-Null
& (Join-Path (Split-Path -Parent $gcc) 'arm-none-eabi-nm.exe') "$env:TEMP\_prep_check.o" |
    Select-String -Pattern 'USB_HP_CAN1_TX_IRQHandler|USB_LP_CAN1_RX0_IRQHandler'
Remove-Item "$env:TEMP\_prep_check.o" -ErrorAction SilentlyContinue
