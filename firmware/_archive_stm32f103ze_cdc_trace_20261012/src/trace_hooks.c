/*
 * etm-trace 钩子实现 —— STM32F103（Cortex-M3）
 *
 * 本文件只做三件事，全部与"外部 trace 工具"配合：
 *   1) 打开 DWT 周期计数器（CYCCNT）—— 给外部工具一个高精度时间戳
 *   2) 提供稳定入口 trace_hook_entry() —— 外部工具在这里下硬件断点当起点
 *   3) 提供 trace_hook_mark() —— 主循环打点，配合 DWT 周期差定位
 *
 * 注意：**这里不配置 ITM/TPIU/ETM**。F103 也没有 ETM，只有 ITM+SWO（单线），
 * 要不要开、开在哪个引脚、SWO 分频多少，全部由外部工具（OpenOCD/pyOCD/上位机）决定。
 * 这样固件在"没有 trace 工具"时也能独立跑，和 stm32h7b0_spi_trace 的做法一致。
 */
#include "trace_hooks.h"
#include "stm32f1xx.h"
#include "SEGGER_RTT.h"

#include <stdarg.h>

/* 固定地址段：外部工具可以直接按地址读，不用查符号表 */
__attribute__((section(".trace_diag"), used)) volatile trace_diag_t g_trace = {
    0x31524354u /* 'TRC1' */, 0, 0, 0, 0, 0, 0, 0, 0
};

/* DWT / DEMCR 寄存器（Cortex-M3） */
#define DEMCR      (*(volatile uint32_t *)0xE000EDFCu)
#define DWT_CTRL   (*(volatile uint32_t *)0xE0001000u)
#define DWT_CYCCNT (*(volatile uint32_t *)0xE0001004u)
#define DWT_LAR    (*(volatile uint32_t *)0xE0001FB0u) /* 仅部分 M3 有（TI/ST 某些批次） */

/* 逻辑分析仪观察脚：PA0（推挽输出，跑起来后每次打点翻转）。
 * 不接 LA 也不影响运行；想换脚改这两行即可。 */
#define TRACE_PIN_PORT GPIOA
#define TRACE_PIN      0u

static uint32_t s_last_cycles;
static uint8_t s_pin_state;

/* system_clock.c 里实测出来的 HCLK */
extern uint32_t g_sysclk_hz;

void trace_hook_init(void)
{
    /* 显式初始化诊断结构体。
     * ⚠️ 实测坑：`.trace_diag` 是放在 RAM 末尾的 NOLOAD 段（启动代码的 bss 清零只覆盖
     * _sbss~_ebss，够不到它），而 C 里的静态初值在本工程链接布局下**没有真正落进 RAM**
     * （实测用 DWT 写观察点确认过：开机后该地址从未被写过）。所以这里必须自己写一遍，
     * 否则外部工具按固定地址读到的永远是上电随机值。 */
    g_trace.magic = 0x31524354u; /* 'TRC1' */
    g_trace.sysclk_hz = 0;
    g_trace.ready = 0;
    g_trace.loops = 0;
    g_trace.marks = 0;
    g_trace.cyc_last = 0;
    g_trace.cyc_delta = 0;
    g_trace.usb_rx = 0;
    g_trace.usb_tx = 0;
    g_trace.usb_irq = 0;
    g_trace.usb_ctr = 0;
    g_trace.usb_reset = 0;
    g_trace.usb_err = 0;
    g_trace.usb_susp = 0;

    /* DWT 解锁：部分 Cortex-M3（含部分 STM32F1 批次）需要先写 LAR。
     * 对没有 LAR 的实现，写这个地址是 RAZ/WI，无害。 */
    DWT_LAR = 0xC5ACCE55u;

    DEMCR |= (1u << 24);   /* TRCENA = 1，DWT/ITM 总开关 */
    DWT_CYCCNT = 0;
    DWT_CTRL |= (1u << 0); /* CYCCNTENA = 1 */

    /* 打点用的 GPIO：并不属于 trace 外设，纯给逻辑分析仪看时间轴 */
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;
    (void)RCC->APB2ENR;
    {
        uint32_t crl = TRACE_PIN_PORT->CRL;
        crl &= ~(0xFu << (TRACE_PIN * 4));
        crl |= (0x1u << (TRACE_PIN * 4)); /* 通用推挽输出，2MHz */
        TRACE_PIN_PORT->CRL = crl;
    }
    TRACE_PIN_PORT->BRR = (1u << TRACE_PIN);
}

void trace_hook_entry(void)
{
    /* 只放一个编译期可见的稳定落点，外部工具在这里下硬件断点。
     * 不要写成 static inline，否则会被优化掉。 */
    __asm volatile("nop");
}

void trace_hook_ready(void)
{
    g_trace.sysclk_hz = g_sysclk_hz;
    g_trace.ready = 1u;
}

void trace_hook_mark(void)
{
    uint32_t now = DWT_CYCCNT;

    g_trace.marks++;
    g_trace.cyc_last = now;
    g_trace.cyc_delta = now - s_last_cycles;
    s_last_cycles = now;

    /* 翻转 PA0：LA 上能看到"打点密度" */
    s_pin_state ^= 1u;
    if (s_pin_state) {
        TRACE_PIN_PORT->BSRR = (1u << TRACE_PIN);
    } else {
        TRACE_PIN_PORT->BRR = (1u << TRACE_PIN);
    }
}
