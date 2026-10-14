# STM32F103 + CherryUSB CDC ACM + etm-trace 钩子

目标：把 CherryUSB 的 `demo/cdc_acm_template.c` 跑到 STM32F103 上，做成 **etm-trace 的被测目标固件**。
当前在线测试板是 **F103CB（64K Flash / 20KB RAM）**，代码同时支持 **F103ZE（512K/64K，带 ETM）**。

- CherryUSB 版本：**v1.6.1**（源码来自 `E:\esp-idf-s31\projects\cherryusb_cdc\managed_components\cherry-embedded__cherryusb`，拷进 `cherryusb/`）
- HAL：ST 官方 STM32F1xx HAL（只挑用到的 11 个模块，**不含** `stm32f1xx_hal_pcd.c`）
- USB port：**fsdev**（F1 的老款 USB FS device 外设；H7 系列才用 dwc2）
- 构建：arm-none-eabi-gcc + `build.ps1`，不依赖 CubeMX / Keil

---

## 1. 构建与烧录

```powershell
# 在线板 F103CB（64K/20K）
pwsh -NoProfile -File .\build.ps1 -Chip cb

# 目标板 F103ZE（512K/64K，带 ETM）
pwsh -NoProfile -File .\build.ps1 -Chip ze

# 烧录（pyocd + CMSIS-DAP 探针，当前用 akaLinkPro）
pwsh -NoProfile -File .\flash.ps1 -Chip cb
```

产物：`build\<chip>\fw.{elf,bin,hex}`，根目录另有 `fw-cb.*` / `fw-ze.*` 副本。
`.bin` 的烧录地址是 `0x08000000`。

实测占用（F103CB 档）：`text 18160 + data 148 = 18308` 字节 Flash，`bss 4768` 字节 RAM。

## 2. 硬件与接线

| 项 | 值 |
|---|---|
| HSE 晶振 | **8 MHz**（必须是 8M 或 12M，见下节） |
| USB | PA11 = DM，PA12 = DP，**全速 12Mbps**，无需外部上拉（芯片内部 1.5k 上拉） |
| SWD | PA13/PA14（与 USB 引脚不冲突，可同时接探针） |
| 打点脚 | **PA1**：启动阶段脉冲；**PA0**：`trace_hook_mark()` 每次翻转 |

## 3. 时钟（F1 做 USB 的硬约束）

```
SYSCLK = HSE(8MHz) × PLLMUL(9)          = 72 MHz
USB    = SYSCLK / 1.5                    = 48 MHz   （RCC_CFGR.USBPRE = 0）
```

- **F1 的 USB 时钟只能来自 PLL**，且预分频只有 `/1.5` 和 `/1` 两档（`RCC_USBCLKSOURCE_PLL_DIV1_5`）。
  要得到 48MHz，SYSCLK 只能是 72MHz（÷1.5）或 48MHz（÷1）。
- 因此 HSE 必须是 **8MHz（×9=72）** 或 **12MHz（×6=72）**；其它频率固件会退回 HSI 继续跑，
  但 USB 不可用（`INFO` / `g_trace.clock_err` 会体现）。构建时可用 `-HseMHz 12` 切换。
- HSI 路线（HSI/2 × 12 = 48MHz）**数值上可行但不合规**：F1 的 HSI 是 RC，精度 ±1% 量级，
  而 USB FS 要求 ±0.25%（USB 2.0 §7.1.11）。**必须用晶振。**

## 4. 已知坑（都踩过，改的时候别改回去）

1. **`SysTick_Handler` 必须实现**。
   ST 的 `startup_stm32f103xb.s` 把所有未实现的向量弱别名到 `Default_Handler`，
   而 `HAL_Init()` 会打开 SysTick → 第一个 1ms 中断就把 CPU 永远困在 `Default_Handler` 里。
   现象：USB 不枚举、`g_trace` 全是 0。已在 `src/stm32f1xx_it.c` 里实现。
2. **先配时钟，再 `HAL_Init()`**。
   `HAL_Init()` 用复位默认的 HSI(8MHz) 算 SysTick 重载值并立刻开中断，之后再把主频抬到 72MHz，
   SysTick 周期就会偏 9 倍。现在顺序是 `system_clock_init()` → `HAL_Init()`。
3. **不要链接 `-nostartfiles`**。
   ST 的 F1 启动文件里有 `bl __libc_init_array`，而 `_init` 由 `crti.o` 提供；
   去掉标准启动文件会 `undefined reference to '_init'`。
4. **`HAL_PCD_MODULE_ENABLED` 必须定义，但不要编 `stm32f1xx_hal_pcd.c`**。
   CherryUSB 的 `usb_dc_fsdev.c` 需要 `stm32f1xx_hal_pcd.h` 里那批 `PCD_xxx` 宏；
   USB 状态机由 CherryUSB 自己实现，HAL 的 PCD 是多余的。
   相应地，`usb_dc_low_level_init()` 是自己写的（**无参**版本，覆盖 fsdev port 里的 `__WEAK`），
   官方那份 `port/fsdev/usb_glue_st.c` 是**带参**版本且要 `HAL_PCD_MspInit`——两者不能同时编。
5. **`CONFIG_USBDEV_FSDEV_PMA_ACCESS` 必须是 2**。
   F1 的 PMA 按 16 位访问（依据 `stm32f1xx_ll_usb.h` 里 `#define PMA_ACCESS 2U`）。
6. **`.trace_diag` / `.rtt_cb` 是 NOLOAD 固定地址段，C 静态初值不可靠**。
   实测（DWT 写观察点验证）：这两个段在 RAM 末尾，启动代码的 bss 清零够不到，
   C 里的静态初值也没有真正落进 RAM。所以 `trace_hook_init()` 里**显式**给 `g_trace` 赋初值，
   RTT 控制块则靠 `SEGGER_RTT_Init()` 自己写签名。
7. **`EXTI->PR` 清 bit18**：`__HAL_GPIO_EXTI_CLEAR_IT()` 只接受 `GPIO_PIN_x` 宏，没有 bit18，
   直接用寄存器写（USB 唤醒占用 EXTI 线 18）。

## 5. 运行验证（不需要 USB 主机也能自证）

固件跑起来后，用 SWD 读固定地址：

| 地址（F103CB） | 内容 | 期望 |
|---|---|---|
| `0x20004fc0` | `g_trace`（36 字节） | `31524354`（=`'TRC1'`）、`044aa200`（=72,000,000 Hz）、`1`（ready） |
| `0x20004bc0` | `_SEGGER_RTT` | `47474553 52205245 00005454`（=`"SEGGER RTT"`） |
| `0x20000014` | `g_boot_stage` | `7` = 8 步启动全部走完（最后一步进主循环前） |

```powershell
python .\tools\min_probe.py --target stm32f103cb --read 0x20004fc0:36 --read 0x20004bc0:12 --read 0x20000014:4
```

`tools/min_probe.py` 用 pyocd 的 Python API（比 `pyocd cmd` 可靠：一次连上、halt、读、resume，
`pyocd cmd` 反复重连时会返回陈旧数据，很容易误判）。
`tools/watch_probe.py` 可以设 DWT 写观察点看"谁在写某个地址"，排查内存被踩很有用。

## 6. CDC 命令

拉 DTR 后（`cdc_test.py` 会自动拉）可用：

| 命令 | 响应 |
|---|---|
| `PING` | `PONG` |
| `INFO` | `chip=STM32F103 sysclk=72000000Hz clk_err=0 loops=... marks=... rx=... tx=...` |
| `MARK` | `OK MARK`（同时调 `trace_hook_mark()` 打一个 trace 点） |
| `ECHO on` / `ECHO off` | 开关"收到什么回什么"（默认 **on**） |
| `ECHO <text>` | 回显 `<text>` |
| 其它非空行 | `ERR UNKNOWN` |

```powershell
python ..\tools\cdc_test.py --list
python ..\tools\cdc_test.py --port COM7          # 全套自检
python ..\tools\cdc_test.py --port COM7 --speed --kb 200
```

## 7. etm-trace 部分

**固件只留钩子，不配置 trace 外设**（与 `stm32h7b0_spi_trace` 同一约定）：

- `trace_hook_entry()`：稳定落点，外部工具在这里下硬件断点当执行流起点
- `trace_hook_ready()`：置 `g_trace.ready = 1`，告诉工具"可以开始采了"
- `trace_hook_mark()`：主循环打点（累计计数 + DWT 周期 + 翻转 PA0 给逻辑分析仪）
- DWT 周期计数器已开（`DWT_CYCCNT`，`trace_hook_cycles()` 可读）

### ⚠️ 关于 F103 的 ETM：型号差别很大，别搞混

| 型号 | Flash | ETM | 依据 |
|---|---|---|---|
| F103C4/C6（小容量，16/32K） | 16–32K | ❌ 无 | DS5319 全文无 ETM |
| **F103RB/C8/CB（中容量，64/128K）** | 64–128K | ❌ **无** | DS5319 Rev 20 Features 只写 `SWD and JTAG` |
| **F103xC/xD/xE（高容量，含 F103ZE）** | 256–512K | ✅ **有 ETM（ETMv3）** | DS5792 Rev 13 Features：`Cortex-M3 Embedded Trace Macrocell`；RM0008 §31.1 |
| F103xF/xG（超大容量） | 768K–1M | ✅ 有 | 同 DS5792 系列 |

- RM0008 §31.15.1 关键结论：**「Data are traced using DWT or ITM whereas instructions are traced
  using the ETM」** —— SWO/ITM 只能做数据/printf 打点，**指令流跟踪必须靠 ETM**。
- 所以：**当前这块 F103CB 板做不了指令流 trace**（只有 ITM/SWO + RTT 打点）；
  等换成 **F103ZE** 板，本工程不用改代码就能接 ETM —— trace 寄存器由外部 SWD 工具配，
  正好和现在"固件不碰 ETM"的设计对上（换成 ZE 时用 `-Chip ze` 重编即可）。
- 无论有没有 ETM，**RTT 日志都能用**：RTT 用普通 RAM 环形缓冲经 AHB-AP 读写，
  **不需要 SWO 引脚**（要 SWO 的是 ITM/printf 重定向）。

## 8. 目录

```
stm32f103ze_cdc_trace/
├── build.ps1                 # 构建（-Chip cb|ze，-HseMHz 8|12）
├── flash.ps1                 # pyocd 烧录
├── ld/stm32f103cb.ld         # 64K/20K
├── ld/stm32f103ze.ld         # 512K/64K
├── src/
│   ├── main.c                # CDC ACM 应用（模板改写）+ 命令行
│   ├── system_clock.c        # 72MHz / USB 48MHz
│   ├── usb_dc_low_level.c    # USB 时钟 + 中断使能（不依赖 HAL PCD）
│   ├── stm32f1xx_it.c        # SysTick + USB_LP 中断 + 异常陷阱
│   ├── trace_hooks.c/.h      # etm-trace 钩子 + DWT + 打点 GPIO
│   ├── boot_probe.c          # 启动阶段探针（PA1 脉冲 + g_boot_stage）
│   └── syscalls.c            # newlib 最小桩
├── sdk/                      # CMSIS + STM32F1xx_HAL_Driver（挑过的）
├── segger_rtt/               # SEGGER RTT（+ 本项目专用 SEGGER_RTT_Conf.h）
├── cherryusb/                # CherryUSB v1.6.1（core/class-cdc/common/port-fsdev/config）
└── tools/
    ├── min_probe.py          # pyocd 读内存（halt/读/可选 resume）
    └── watch_probe.py        # DWT 写观察点
```
