# H743 烧录 + 校验（OpenOCD + CMSIS-DAP）。末尾用 AIRCR 软复位而不是 reset halt，
# 因为本板 nRESET 未必接到探针。用法：
#   pwsh -NoProfile -File tmp\h743-flash.ps1 [-Elf <path>]
param([string]$Elf = (Join-Path $PSScriptRoot '../../firmware/stm32h743_cdc_trace/build/fw.elf'))
$ocd = 'E:\Share\env-windows\rtt-debugger-support-package-0.4.0\bin\openocd.exe'
$scr = 'E:\Share\env-windows\rtt-debugger-support-package-0.4.0\bin\scripts'
$a = @('-s', $scr, '-f', 'interface/cmsis-dap.cfg', '-c', 'transport select swd',
       '-f', 'target/stm32h7x.cfg', '-c', 'adapter speed 1000',
       '-c', 'init', '-c', 'halt',
       '-c', 'mww 0xE000ED0C 0x05FA0004', '-c', 'sleep 300', '-c', 'halt',
       '-c', 'mww 0xE000E010 0')
for ($i = 0; $i -lt 8; $i++) { $a += @('-c', ('mww 0x{0:X} 0xFFFFFFFF' -f (0xE000E180 + $i * 4))) }
$a += @('-c', "flash write_image erase `"$Elf`"",
        '-c', "verify_image `"$Elf`"",
        '-c', 'mww 0xE000ED0C 0x05FA0004', '-c', 'sleep 300',
        '-c', 'shutdown') # AIRCR already released the CPU; resume on a running core fails.
$out = & $ocd @a 2>&1 | Out-String
$out -split "`n" | Where-Object {
    $_ -notmatch 'DEPRECATED|Licensed|bugs.html|Open On-Chip|^Info : CMSIS|^Info : SWCLK|^Info : clock|^Info : Using|^Info : SWD|gdb|^Info : Listening|^Info : starting|^$'
} | ForEach-Object { $_.TrimEnd() }
if ($LASTEXITCODE -ne 0) { Write-Error 'OpenOCD failed'; exit 1 }
if ($out -notmatch 'verified (\d+) bytes') { Write-Error 'flash verify FAILED'; exit 1 }
