param([ValidateSet('280','140','70')][string]$CoreMHz='280',[ValidateSet(0,4,5,6,8,16)][int]$TraceR=0,[string]$Gcc)
$ErrorActionPreference='Stop'
$spiRoot=$PSScriptRoot
if (-not $Gcc) {$Gcc=(Get-Command arm-none-eabi-gcc).Source}
$spiBin=Split-Path -Parent $Gcc
$spiSdk=Join-Path $spiRoot 'sdk'
$spiProfile=$CoreMHz
if ($TraceR -ne 0) {$spiProfile="$CoreMHz-r$TraceR"}
$spiBuild=Join-Path $spiRoot "build\$spiProfile"
New-Item -ItemType Directory -Path $spiBuild -Force | Out-Null
$spiP=2; $spiR=4
if ($CoreMHz -eq '140') {$spiP=4; $spiR=4}
if ($CoreMHz -eq '70') {$spiP=8; $spiR=8}
if ($TraceR -ne 0) {$spiR=$TraceR}
$spiFlags=@('-mcpu=cortex-m7','-mthumb','-mfpu=fpv5-d16','-mfloat-abi=hard',
 '-Og','-g3','-gdwarf-4','-fno-inline','-fno-optimize-sibling-calls',
 '-fno-if-conversion','-fno-if-conversion2','-ffunction-sections','-fdata-sections','-fno-common',
 '-Wall','-Wextra','-Wno-unused-parameter','-Wno-unused-function',
 '-DUSE_HAL_DRIVER','-DSTM32H7B0xx','-DHSE_VALUE=25000000',
 "-DSPI_TRACE_PLL_P=$spiP","-DSPI_TRACE_PLL_R=$spiR",
 "-I$spiRoot\src","-I$spiSdk\Core\Inc","-I$spiSdk\RTT_TRACE_LOG",
 "-I$spiSdk\Drivers\STM32H7xx_HAL_Driver\Inc","-I$spiSdk\Drivers\STM32H7xx_HAL_Driver\Inc\Legacy",
 "-I$spiSdk\Drivers\CMSIS\Device\ST\STM32H7xx\Include","-I$spiSdk\Drivers\CMSIS\Include")
$spiSources=@("$spiSdk\Core\Src\main.c","$spiSdk\Core\Src\system_stm32h7xx.c",
 "$spiSdk\Core\Src\stm32h7xx_hal_msp.c","$spiSdk\Core\Src\stm32h7xx_it.c",
 "$spiRoot\src\spi_trace_support.c","$spiRoot\src\syscalls.c",
 "$spiSdk\RTT_TRACE_LOG\SEGGER_RTT.c","$spiSdk\RTT_TRACE_LOG\SEGGER_RTT_printf.c","$spiSdk\RTT_TRACE_LOG\trace_log.c",
 "$spiSdk\Drivers\CMSIS\Device\ST\STM32H7xx\Source\Templates\gcc\startup_stm32h7b0xx.s")
foreach ($spiHal in @('hal','hal_cortex','hal_rcc','hal_rcc_ex','hal_pwr','hal_pwr_ex','hal_flash','hal_flash_ex',
 'hal_gpio','hal_dma','hal_dma_ex','hal_mdma','hal_hsem','hal_spi','hal_spi_ex','hal_uart','hal_uart_ex',
 'hal_i2c','hal_i2c_ex','hal_i2s','hal_i2s_ex','hal_ltdc','hal_ltdc_ex','hal_ospi','hal_rtc','hal_rtc_ex',
 'hal_sd','hal_sd_ex','hal_sdram','ll_fmc','ll_sdmmc')) {
 $spiSources+=Join-Path $spiSdk "Drivers\STM32H7xx_HAL_Driver\Src\stm32h7xx_$spiHal.c"
}
$spiObjects=@()
foreach ($spiSource in $spiSources) {
 $spiObject=Join-Path $spiBuild ([IO.Path]::GetFileNameWithoutExtension($spiSource)+'.o')
 & $Gcc @spiFlags -c $spiSource -o $spiObject
 if ($LASTEXITCODE -ne 0) {throw "编译失败：$spiSource"}
 $spiObjects+=$spiObject
}
$spiElf=Join-Path $spiBuild 'fw.elf'
& $Gcc @spiFlags @spiObjects "-T$spiRoot\ld\stm32h7b0.ld" '-nostartfiles' '-specs=nano.specs' '-specs=nosys.specs' '-Wl,--gc-sections' "-Wl,-Map=$spiBuild\fw.map" -o $spiElf
if ($LASTEXITCODE -ne 0) {throw '链接失败'}
foreach ($spiFormat in @(@('binary','bin'),@('ihex','hex'))) {
 & "$spiBin\arm-none-eabi-objcopy.exe" -O $spiFormat[0] $spiElf "$spiBuild\fw.$($spiFormat[1])"
 if ($LASTEXITCODE -ne 0) {throw '转换失败'}
}
& "$spiBin\arm-none-eabi-size.exe" $spiElf
& "$spiBin\arm-none-eabi-objdump.exe" -d -S $spiElf | Set-Content -LiteralPath "$spiBuild\fw.disasm.txt" -Encoding utf8
& "$spiBin\arm-none-eabi-nm.exe" -n -S $spiElf | Set-Content -LiteralPath "$spiBuild\fw.symbols.txt" -Encoding utf8
foreach ($spiExtension in @('elf','bin','hex')) {Copy-Item -LiteralPath "$spiBuild\fw.$spiExtension" -Destination "$spiRoot\fw-$spiProfile.$spiExtension" -Force}
Write-Output "CPU $CoreMHz MHz；物理 TRACECLK $(280/$spiR) MHz；固件不自行开启 ETM。"
