# STM32H7B0 CherryUSB CDC ACM + ETM 被测目标固件

参考同级 `stm32h7b0_spi_trace`：固件初始化 CPU/USB 时钟、CDC 和诊断信息，网页通过 SWD 配置并开启 ETM、CSTF、ETF、TPIU 和 PE2～PE6。普通上电不会由固件自行配置 ETM。目标板使用 **25MHz HSE、LDO 供电**。

## 构建

```powershell
./build.ps1                            # CPU 280MHz / 引脚 TRACECLK 70MHz
./build.ps1 -CoreMHz 70                # CPU 70MHz / TRACECLK 35MHz，与 SPI 稳定基准同档
./build.ps1 -CoreMHz 140 -TraceR 8      # CPU 140MHz / TRACECLK 35MHz
./build.ps1 -CoreMHz 280 -TraceR 8      # CPU 280MHz / TRACECLK 35MHz
```

产物分别为 `build/<档位>/fw.{elf,bin,hex}` 和 `fw-<档位>.{elf,bin,hex}`；根目录 `fw.{elf,bin,hex}` 和 `build/fw.*` 是最近一次构建的副本。BIN 烧录地址为 `0x08000000`。构建只编译，不烧录。网页使用的 ELF 必须与实际烧录的固件一致。

| 构建参数 | CPU | TRACEPORTCK | 引脚 TRACECLK |
|---|---:|---:|---:|
| 默认 | 280MHz | 140MHz | 70MHz |
| `-CoreMHz 70` | 70MHz | 70MHz | 35MHz |
| `-CoreMHz 140` | 140MHz | 140MHz | 70MHz |
| `-CoreMHz 140 -TraceR 8` | 140MHz | 70MHz | 35MHz |
| `-CoreMHz 280 -TraceR 8` | 280MHz | 70MHz | 35MHz |

PLL1：25MHz / 5 × 112 = 560MHz VCO；CPU = VCO/P；TRACEPORTCK = VCO/R；引脚 TRACECLK = TRACEPORTCK/2。P/R 使用构建脚本传入的宏。HCLK = CPU/2；APB1/2/3 = HCLK/2；APB4 = HCLK/4。PLL2 未使用。USB 使用 PLL3：25MHz / 5 × 48 / 5 = **48MHz**，不依赖未同步的 HSI48，也不随 CPU/Trace 档位改变。

## USB 引脚和供电

H7B0 使用 `USB1_OTG_HS`（`0x40040000`），PA11/PA12 为内嵌 FS PHY；AF10 为 `GPIO_AF10_OTG1_FS`，中断为 `OTG_HS_IRQn`，CDC 最大包为 64 字节。VID/PID 为 `F055:7B01`，打开串口后需拉高 DTR。

默认 `-UsbSupply External`：VDD33USB 与 VDD50USB 接外部 3.3V，关闭内部 USB 稳压器。若 VDD50USB 接 5V、VDD33USB 由内部稳压器输出，使用：

```powershell
./build.ps1 -CoreMHz 70 -UsbSupply Internal
```

对应产物为 `fw-70-usb-internal.*`。两个配置均开启 USB 电压检测；内部稳压器等待就绪超时会记录启动错误。USB 数据线接好不代表确认了供电接法，需按实际原理图选择参数。

本工程使用 FIFO/中断模式，不启用 DWC2 DMA；不能按 PA11/PA12 或 PB14/PB15 的引脚名称判断 DMA 能力。D-Cache 关闭、I-Cache 开启。`.noncacheable` 只是链接段名，**不会自动改变 AXI SRAM 的缓存属性**；以后开启 D-Cache 时必须同步配置 MPU/缓存维护策略。

## CDC 命令和收发

命令使用 LF 或 CRLF 结尾，可跨 USB 包，也可在同一包内发送多条命令。

| 命令 | 回复 |
|---|---|
| `PING` | `PONG` + CRLF |
| `INFO` | 芯片、CPU/TRACECLK、时钟错误、循环/打点及收发计数 |
| `MARK` | PA0 翻转，回复 `OK MARK` + CRLF |
| `ECHO off` / `ECHO on` | 关闭/开启原始回显，回复 `OK ECHO off/on` + CRLF |
| `ECHO <text>` | 回复正文及 CRLF，不受原始回显开关影响 |

默认原始回显开启，命令回声和命令回复按顺序排队。关闭回显后，未知命令回复 `ERR UNKNOWN`，过长文本行回复 `ERR LINE TOO LONG`。命令行最多 127 字节（不含 LF，包含可选的 CR）。含非文本字节的行不作为命令解析。回显开启时，普通未知数据只原样返回；命令关键字为保留文本序列。切换原始数据到命令时，先发送换行以结束前一行。缺少换行的命令不会执行。

发送使用 4096 字节队列和独立端点暂存区，忙时保留数据。每次接收一个 64 字节包，主循环消费完后重新接收；发送队列满时暂停接收，由 USB OUT NAK 提供背压。主循环与 reset/DTR 回调通过短临界区协调。64 字节整数倍的 IN 传输在 ZLP 完成后才复用暂存区。总线复位/取消配置/断开会清理会话；DTR 关闭会清理命令和待发送队列，再次打开后恢复默认回显并重新发送欢迎信息。

RTT 为非阻塞 SKIP 模式，缓冲满时丢弃日志，不会卡住 CDC。RTT 在首次日志前初始化；`src/runtime.c` 提供真正的 `_init()`，不再跳到地址 0。USB/时钟初始化失败不会置 `ready=1`。

## ETM 与诊断信息

初始化完成后调用 `trace_hook_ready()`，然后调用稳定业务入口 `trace_hook_entry()`，随后进入 CDC 主循环。网页可在 `trace_hook_entry` 设置硬件断点：此时 RCC/USB 初始化和启动日志已完成，`g_trace.ready=1`。配置并回读 ETM 后再继续 CPU，与 SPI 例程的业务入口流程一致。DWT 初始化包含 M7 软件解锁；PA0 为打点脚，PA1 为启动探针。移除了不符合该工作方式的 `trace_etm_off()`；停止/刷新 trace 由外部工具负责。

`g_trace` 固定在 DTCM `0x20003000`，共 56 字节；原有前 40 字节布局保留：

| 偏移 | 字段 |
|---:|---|
| 0 | magic = `0x34524354` |
| 4 | sysclk_hz |
| 8 | ready：初始化完成，不代表 USB 已枚举或 ETM 已开启 |
| 12 / 16 | loops / marks |
| 20 / 24 | cyc_last / cyc_delta |
| 28 / 32 | usb_rx / usb_tx：累计实际传输字节 |
| 36 | etm_touched：固件不配置 ETM/TPIU，为 0；不是硬件状态校验 |
| 40 / 44 / 48 | hclk_hz / traceportck_hz / traceclk_hz |
| 52 | error：正常为 0 |

RTT 控制块固定在 `0x20002000`。`g_boot_stage=8` 表示初始化完成；其地址从对应档位的 `fw.symbols.txt` 查找，不能依赖旧版本的 `0x24000014`。`error=0x100` 为 USB 初始化失败；其它值为 `g_clock_err` 位掩码：1=HSE/PLL1，2=系统时钟，4=USB PLL3，8=内部 USB 稳压器，16=LDO/VOS，32=时钟回读不符。

外部工具按 **RM0455 第64章** 配置：CSTF/ETF/TPIU 系统总线地址分别为 `0x5C013000`、`0x5C014000`、`0x5C015000`；调试地址分别为 `0xE00F3000`、`0xE00F4000`、`0xE00F5000`。ETM 为 `0xE0041000`；TRCPRGCTLR +0x004、TRCSTATR +0x00C、TRCCONFIGR +0x010、TRCVICTLR +0x080。并行 TPIU 宽度通过 +0x004 设置，四位输出写 `0x8`；不能使用 SWO 的 SPPR/ACPR。PE2=TRACECLK、PE3～PE6=D0～D3，AF0；还需使能 DBGMCU Trace 时钟、解锁并设置 ETM→CSTF→ETF→TPIU 通路。停止 ETM 后等待 IDLE=1，等待应有超时。

## 验证

```powershell
./tests/run_tests.ps1                   # Windows 本机 gcc，8 项 CDC 回归场景
python ../tools/cdc_test.py --list
python ../tools/cdc_test.py --port COMx
python ../tools/cdc_test.py --port COMx --speed --kb 200
```

本机测试覆盖默认回显与回复、拆包/合包命令、显式回显、未知命令、长行/二进制隔离、50KB 队列背压及环绕、发送重试保留、会话复位。这些测试和交叉编译不能代替目标板上的枚举、ZLP、中断时序和 ETM 波形验证。USB 实测使用 `F055:7B01` 的目标 CDC 口；调试探针的串口不是目标 CDC。

参考：[SPI 例程](../stm32h7b0_spi_trace/README.md)、[ST RM0455](https://www.st.com/resource/en/reference_manual/dm00463927-stm32h7a37b3-and-stm32h7b0-value-line-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)、[Arm ETM-M7 手册](https://documentation-service.arm.com/static/5e906dfe8259fe2368e2ac54)。

