# STM32H743 + CherryUSB CDC ACM + etm-trace 钩子

目标板：**正点原子阿波罗 STM32H743**（HSE 25MHz）。
与 `stm32h7b0_cdc_trace` 是**同一套源码结构**，差别只有：芯片宏、启动文件、链接脚本容量、
时钟（480MHz）、USB 实例（用 `USB_OTG_FS` 而不是 H7B0 那个唯一的 `USB1_OTG_HS`）。

## 1. 构建

```powershell
pwsh -NoProfile -File .\build.ps1              # CPU 480MHz
pwsh -NoProfile -File .\build.ps1 -TraceR 4    # TRACEPORTCK 240MHz
```

产物 `build/fw.{elf,bin,hex}` + 根目录副本；`.bin` 烧到 `0x08000000`。
实测占用：`text 31168 + data 144 ≈ 31.3KB`（2MB Flash 随便放）。

## 2. 时钟（480MHz，阿波罗板标准做法）

```
HSE 25MHz
└ PLL1: M=5, N=192, P=2, Q=2, R=2
    VCO = 25/5 × 192 = 960 MHz
    SYSCLK = 960/P = **480 MHz**
    AXI/HCLK = /2 = 240 MHz；APB1/2/3 = /2 = 120 MHz；APB4 = /4 = 60 MHz
USB : HSI48 直供 48 MHz
```

⚠️ 跑 480MHz 有两条硬要求（官方 RTT 测速固件里记过，本工程已照做）：

1. **VOS0 电压档**，且写 `VOS` 之前必须先清 `PWR_CR3.SCUEN` ——
   不清的话 `VOS` 写入被硬件静默忽略，表现是永远等不到 `VOSRDY`。
2. 升频前先把 **Flash 等待周期**加上（这里 4WS）。

## 3. USB 接线（阿波罗板特有，最容易踩）

- 用 **USB_OTG_FS @0x40080000，PA11/PA12，内嵌全速 PHY**。
- ⚠️ **板上 USB HOST 与 USB SLAVE 共用 PA11/PA12**，靠**跳线帽**选择：
  要当设备用，跳线帽必须把 PA11/PA12 接到 **USB_SLAVE** 座子，并且线插在 SLAVE 口。
- ⚠️ **板上没有 ULPI PHY**，所以走不了 USB 高速；HS 口（PB14/PB15）在这块板上用不上。
  代码里 `HAL_PCD_MspInit` 保留了 HS 分支，将来换带 ULPI 的板子不用重写。

## 4. 与 H7B0 工程的差异（对照表）

| 项 | H743（本工程） | H7B0 |
|---|---|---|
| 芯片宏 / 启动文件 | `STM32H743xx` / `startup_stm32h743xx.s` | `STM32H7B0xx` / `startup_stm32h7b0xx.s` |
| 链接脚本 | `ld/stm32h743.ld`（2MB Flash / 512KB AXI） | `ld/stm32h7b0.ld`（128KB / 1MB AXI） |
| SYSCLK | 480MHz（PLL1 N=192, VCO 960） | 280MHz（PLL1 N=112, VCO 560） |
| USB 实例 | `USB_OTG_FS`（0x40080000，`OTG_FS_IRQn`） | `USB1_OTG_HS`（0x40040000，`OTG_HS_IRQn`） |
| USB 低级初始化 | 按 `reg_base` 同时兼容 FS/HS 两个实例 | 只处理 HS |

其余（CherryUSB 移植层、`.noncacheable` 缓冲、`.rtt_cb@0x20002000`、`.trace_diag@0x20003000`、
`boot_probe`、`trace_hooks`、CDC 命令行）**完全一致**，可以互相对照抄。

## 5. 运行验证（无需 USB 主机，也不需要插着 USB 线）

| 地址 | 内容 | 期望 |
|---|---|---|
| `0x20003000` | `g_trace`（40 字节） | `34524354`（`'TRC4'`）、`sysclk=480000000`、`ready=1` |
| `0x20002000` | `_SEGGER_RTT` | `"SEGGER RTT"` |
| `0x24000014` | `g_boot_stage` | `8` = 9 步启动全部走完 |

```powershell
python ..\stm32f103ze_cdc_trace\tools\min_probe.py --target stm32h743xx --read 0x20003000:40 --read 0x20002000:12
```

## 6. etm-trace 部分

**固件不配置 ETM**（同 H7B0 工程）：只给 `trace_hook_entry()` 稳定落点、
`g_trace.ready` 就绪标志、DWT 周期打点、PA0 逻辑分析仪打点脚。
ETMv4 / CSTF / ETF / TPIU / PE2–PE6 全部由外部 SWD 工具配置。

参考现成做法：`E:\web-etm-trace\firmware\stm32h7b0_etm`（固件自己配 ETM 的低频验证版）
与本工程所属的网页工具链（`E:\web-etm-trace\tools`、`server.py`），
H7B0 上的接线与寄存器结论可直接迁移到 H743（同为 Cortex-M7 + ETMv4 + TPIU）。

## 7. 目录

```
stm32h743_cdc_trace/
├── build.ps1 / ld/stm32h743.ld
├── src/{main.c, trace_hooks.c/.h, boot_probe.c, usb_dc_low_level.c}
├── sdk/            # H7 HAL/CMSIS/RTT（H743 与 H7B0 共用同一份 STM32H7xx HAL）
├── cherryusb/      # v1.6.1；port/dwc2 用自写的 device-only glue
└── build/
```

## 8. 已知坑

与 `stm32h7b0_cdc_trace/README.md` 第 4 节完全一致（官方 glue 会拖入 host 栈、
`HAL_PCD_MODULE_ENABLED`、`-DRTT_CB_ATTR`、`PROVIDE(_init=0)`、D-Cache 关闭、RTT 256KB 上行缓冲）。
额外一条：H743 头文件里 `USB_OTG_HS` 是 `USB1_OTG_HS` 的别名、`USB_OTG_FS` 是 `USB2_OTG_FS` 的别名，
写代码时用哪个名字都行，但**中断向量要用 `OTG_FS_IRQn`/`OTG_HS_IRQn` 这对名字**。
