# H7B0 SPI 执行流验证固件

源工程：`E:\Share\github\STM32H7xx_Develop\App_SPI_H7B0`。`reference/` 保存原工程与原 ARMCC AXF；`sdk/` 是工作副本。原工程没有被修改。

目标板条件：用户确认 **25 MHz HSE、LDO 供电**。新构建使用 `STM32H7B0xx`、ST 的非 Q H7B0 GCC 启动文件、128 KiB Flash 链接上限。原 H7B3 CubeMX 文件仅作为 `sdk/App.original-H7B3.ioc` 留存，不能直接用它重新生成此验证固件。

## 构建档位

| 命令 | CPU | 物理 TRACECLK | 根目录 ELF |
|---|---:|---:|---|
| `./build.ps1 -CoreMHz 70` | 70 MHz | 35 MHz | `fw-70.elf` |
| `./build.ps1 -CoreMHz 140` | 140 MHz | 70 MHz | `fw-140.elf` |
| `./build.ps1 -CoreMHz 140 -TraceR 8` | 140 MHz | 35 MHz | `fw-140-r8.elf` |
| `./build.ps1 -CoreMHz 280` | 280 MHz | 70 MHz | `fw-280.elf` |
| `./build.ps1 -CoreMHz 280 -TraceR 5` | 280 MHz | 56 MHz | `fw-280-r5.elf` |
| `./build.ps1 -CoreMHz 280 -TraceR 6` | 280 MHz | 46.667 MHz | `fw-280-r6.elf` |
| `./build.ps1 -CoreMHz 280 -TraceR 8` | 280 MHz | 35 MHz | `fw-280-r8.elf` |

对应 BIN/HEX 同名，构建中间文件按档位存放在 `build/`。只编译不会烧录。

当前固定 **stable-70-35-v1/fw.elf：CPU 70 MHz / trace 35 MHz**，500 MSa/s、取样位置 0.25，CH0～4 的滤波快照为 1 点。已完成 1 ms 复验与 10 ms 全地址业务窗口核对，见 [稳定基准说明](../../baselines/spi-70-35-v1/README.md)。按用户要求暂停频率优化。280/35 MHz 是历史已验收档；46.667 MHz 对相位敏感，56/70 MHz 在当前接线下未通过帧校验，保留为实验记录。

PLL1 输入为 25/5=5 MHz，N=112，VCO=560 MHz，P 决定 CPU，R 决定 TRACEPORTCK。HCLK 为 CPU/2。PLL2 输入也是 5 MHz，N=80，P/Q=2，SPI2 内核为 200 MHz，SPI /4 为标称 50 MHz。固件普通启动不配置或开启 ETM；网页通过 SWD 配置 trace。

## 与原工程的区别

- 原工程按真实 25 MHz 外晶会运行在约 291.667 MHz CPU；此版本修正到 280 MHz，并统一 HAL/CMSIS 的 HSE 定义。
- 按用户确认改为 LDO；不使用原来的直接 SMPS 初始化。
- 保留原 SPI2 轮询发送 100 字节、PC8 标记、空循环、RTT、UART、MPU、缓存和 SysTick；没有改成 SPI DMA。
- 使用 Arm GCC，`-Og -g3 -gdwarf-4 -fno-inline -fno-optimize-sibling-calls -fno-if-conversion -fno-if-conversion2`。此 ELF 不是原 ARMCC AXF，也不是原优化性能的复现；GCC 仍会生成部分 IT 条件指令，解码端明确标记其未知状态。
- `spi_trace_ready()` 提供稳定的业务入口断点；增加每次发送的状态和交易计数，用于当前时刻的 SWD 核对。
- `_SEGGER_RTT` 保留在 DTCM 0x20002000。`g_spi_trace` 放在 DTCM 0x20003000，避开数据缓存；main 显式初始化它。RTT 数据缓冲仍在 AXI SRAM，本轮不以 SWD 读取其历史内容作 ETM 验收。
- 未调用的 256 KiB RTT 测速数组及函数被链接回收。256 KiB RTT 上行缓冲仍保留，属于 SRAM；新 BIN 约 20 KiB。
- GCC 下增加 UART 输出和堆分配适配，使用完整的 ST 向量表。启动文件的汇编调试信息可能为 DWARF 2，C 文件为 DWARF 4，读取器兼容这两种。

## 接线与操作

CH0=PE2/TRACECLK；CH1～4=PE3～PE6/D0～D3，共地。SPI2 为 PA12/SCK、PC2/MISO、PC3/MOSI，PC8 为发送窗口标记。

先烧录目标档位，在独立页面选择同一份验证固件，HSE 填 25，LA 采样率选择 500 MSa/s，时长先用 1 ms。勾选 SWD 联动后，网页会校验 Flash、配置并回读 ETM，再启动 LA 和继续 CPU。CPU、PLL、外设时钟由固件初始化；网页只读取和计算。

业务入口复位使用临时硬件断点，不改写 Flash；复位前先停止和刷新旧 trace，再允许固件切换 RCC 时钟。复位入口暂停/启动全程捕获尚未通过本板验收，目前在页面禁用。

不能仅凭“零解码错误”判断无损。频率和相位的实测结论、包覆盖、未知条件状态及源码执行流，见项目 `docs/validation/` 和 SPI 验收报告。

原始目标 Flash 的完整 128 KiB 备份为项目 `docs/validation/2026-10-11-before-spi-flash-128k.bin`。如需恢复，可使用专门的 `tools/flash_spi.py restore`；执行前需要确保目标已恢复 SWD 访问。
