/*
 * etm-trace 钩子（F1 系列 / Cortex-M3）
 *
 * 设计原则与 stm32h7b0_spi_trace 一致：**固件不自己开 trace 相关的外设配置**，
 * 只提供稳定的观察入口，具体的 trace 采集由外部工具（SWD + OpenOCD/上位机）决定：
 *   - trace_hook_entry()   业务入口，稳定的断点/起点标记
 *   - trace_hook_ready()   跑完初始化后置位，外部工具轮询它判断"可以开始采了"
 *   - trace_hook_mark()    循环里打点，配合 DWT 周期计数定位
 *
 * ⚠️ 关于 Cortex-M3 的 trace 能力（重要，别踩）：
 *   Cortex-M3 是 **ITM + DWT + SWO（单线）**，**没有 ETM/TPIU**。
 *   —— 也就是没有"指令流 trace"（不像 H7 的 ETMv4 能恢复完整执行路径），
 *      能做的是：ITM printf 打点、DWT 周期/事件计数、DWT 数据观察点、PC 采样。
 *   所以本工程在 M3 上给的是"打点/计数"型 trace 钩子；真正的 ETM 要等 F103ZE 板
 *   （或换成 F4/H7 等带 ETM 的型号）再启用，这里先把接口预留好。
 *
 * RTT 说明：`trace_log_printf` 走 SEGGER RTT —— RTT 用普通 RAM 环形缓冲经 AHB-AP
 * 读写，**不需要 SWO 引脚**，F103 上开箱可用；ITM/printf 重定向才是需要 PB3(SWO) 的。
 */
#ifndef __TRACE_HOOKS_H
#define __TRACE_HOOKS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 诊断变量（放 .trace_diag，便于外部工具按固定地址读） */
typedef struct {
    uint32_t magic;      /* 'T','R','C','1' */
    uint32_t sysclk_hz;  /* 实测 SYSCLK */
    uint32_t ready;      /* trace_hook_ready() 置 1 */
    uint32_t loops;      /* 主循环轮数 */
    uint32_t marks;      /* trace_hook_mark() 调用次数 */
    uint32_t cyc_last;   /* 最近一次打点的 DWT 周期数 */
    uint32_t cyc_delta;  /* 与上一次打点的周期差 */
    uint32_t usb_rx;     /* CDC 收到的字节数 */
    uint32_t usb_tx;     /* CDC 发出的字节数 */
    /* USB 中断计数 —— 排查"枚举卡在取描述符"时最关键的一组证据：
     * 对比 host 侧是否真的发出过 RESET/SETUP，就能判断是硬件/接线问题还是驱动问题。 */
    uint32_t usb_irq;    /* USB_LP 中断进入次数 */
    uint32_t usb_ctr;    /* CTR（传输完成，含 SETUP）次数 */
    uint32_t usb_reset;  /* RESET 次数 */
    uint32_t usb_err;    /* ERR 次数 */
    uint32_t usb_susp;   /* SUSP 次数 */
} trace_diag_t;

extern volatile trace_diag_t g_trace;

/* 时钟/看门狗等基础初始化之后调用；打开 DWT 周期计数器 */
void trace_hook_init(void);

/* 业务入口：外部工具在这里下硬件断点，作为执行流起点 */
void trace_hook_entry(void);

/* 置位 g_trace.ready，告诉外部工具"现在可以开始采" */
void trace_hook_ready(void);

/* 周期性打点：累加计数 + 记录 DWT 周期，可选拉一个 GPIO 给逻辑分析仪看 */
void trace_hook_mark(void);

/* 读 DWT 周期计数（Cortex-M3 支持 DWT_CYCCNT） */
static inline uint32_t trace_hook_cycles(void)
{
    return *(volatile uint32_t *)0xE0001004u; /* DWT_CYCCNT */
}

/* trace 相关输出（RTT），也给 CherryUSB 的 CONFIG_USB_PRINTF 用 */
int trace_log_printf(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* __TRACE_HOOKS_H */
