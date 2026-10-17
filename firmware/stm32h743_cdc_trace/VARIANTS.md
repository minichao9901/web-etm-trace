# H743 CDC+ETM 固件：多种频率预编译 ELF

本目录下的 `fw-cpuNN-traceNN.elf/.bin/.hex` 是**同一份源码、不同时钟档**的构建产物，
可直接喂给网页工具（或 `tools/h743_etm.py`）做 trace。

统一条件（四个档都一样）：

- **AHB = CPU**（`RCC_HCLK_DIV1`，HPRE=/1，D1CPRE=/1）⇒ HCLK == CPU 频率
- **TRACECLK = VCO / (2×R) = 30 MHz**（来自 PLL1R；30 MHz 是这块板的实测上限，见下）
- USB 内核时钟 **PLL3Q = 48 MHz**；USB 供电 External；**TRDT 按 HCLK 查表**（已修，不再用 CPU 频率）
- 构建脚本：`build.ps1 -CoreMHz <CPU> -TraceR <R> -AhbDiv 1 -Trdt Hclk -UsbSupply External`

| 文件 | CPU | TRACECLK | HCLK(=AHB) | VCO | M / N / P / R | TRDT | 实测结论 |
|---|---:|---:|---:|---:|---|---:|---|
| `fw-cpu30-trace30.elf` | 30 MHz | 30 MHz | 30 MHz | 240 | 5 / 48 / 8 / 4 | **0x7** | ✅ 同频（CPU=TRACECLK）可解；5ms 窗 11,295 条、50ms 窗 82,109 条 |
| `fw-cpu60-trace30.elf` | 60 MHz | 30 MHz | 60 MHz | 480 | 5 / 96 / 8 / 8 | 0x6 | ✅ **推荐档**；50ms 窗 **326,109 条**，字节零丢失 |
| `fw-cpu90-trace30.elf` | 90 MHz | 30 MHz | 90 MHz | 720 | 5 / 144 / 8 / 12 | 0x6 | ✅ 可用（5ms 量级未单独跑满 50ms） |
| `fw-cpu120-trace30.elf` | 120 MHz | 30 MHz | 120 MHz | 960 | 5 / 192 / 8 / 16 | 0x6 | ✅ 5ms 窗 68,403 条，USB 栈函数全解出 |

## 用的时候要填给网页/工具的数字

- **expected CPU** = 表里的 CPU
- **expected TRACECLK** = 30 MHz（四档都一样）
- LA 采样率建议 **500 MSa/s**（30 MHz 下 16.7 点/周期）；窗口按需要 5 ms ～ 秒级
- LA 接线：`CH0=PE2=TRACECLK`、`CH1=PE3=D0`、`CH2=PE4=D1`、`CH3=PE5=D2`、`CH4=PE6=D3`

## 关键提醒

1. **TRACECLK 不要超过 30–35 MHz**：40 MHz 在本板 + 当前接线 + 500 MSa/s LA 上**所有取样相位都解不出**
   （已用"CPU480 + TRACECLK40"的密集流对照排除是解析问题，属物理上限）。
2. **CPU 越高，同样墙钟时间内抓到的指令越多**（同窗 5ms：30→11,295、60→33,848、120→68,403 条），
   但**同频档（30/30）也是完全可解、字节零丢失**的，只是同墙钟内 CPU 执行得少。
3. 换档烧录后 CDC 会重新枚举（VID `F055` / PID `7431`），约 0.6 s 后 COM 口出现。
4. 板子自检：`pwsh tools/h743/ocd.ps1 -Cmds 'mdw 0x20003000 3'` → 期望 `34524354 <sysclk> 00000001`。
5. 这四个 ELF 的 **HCLK == CPU**；若要测"AHB 分频"（如 USB TRDT 场景）用
   `build.ps1 -CoreMHz 120 -TraceR 16 -AhbDiv 4`（HCLK=30）另build。

## 重新生成

```powershell
cd E:\web-etm-trace\firmware\stm32h743_cdc_trace
$gcc = 'E:/Share/env-windows/tools/gnu_gcc/arm_gcc/mingw/bin/arm-none-eabi-gcc.exe'
foreach ($v in @(@{n='cpu30-trace30';core=30;r=4}, @{n='cpu60-trace30';core=60;r=8},
                 @{n='cpu90-trace30';core=90;r=12}, @{n='cpu120-trace30';core=120;r=16})) {
    pwsh -NoProfile -File .\build.ps1 -CoreMHz $v.core -TraceR $v.r -AhbDiv 1 -Trdt Hclk -UsbSupply External -Gcc $gcc
    Copy-Item build\fw.elf "fw-$($v.n).elf" -Force
    Copy-Item build\fw.bin "fw-$($v.n).bin" -Force
    Copy-Item build\fw.hex "fw-$($v.n).hex" -Force
}
```

> 注意：`build.ps1` 每次都会覆盖 `build/fw.*` 和目录根的 `fw.elf/fw.bin/fw.hex`（不带档位后缀的那些）。
> 带后缀的 `fw-cpuNN-traceNN.*` 是本表这四份，手改 `build.ps1` 时别把它们清掉。
