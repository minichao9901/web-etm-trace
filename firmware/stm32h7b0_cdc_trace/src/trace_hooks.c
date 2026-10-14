/*
 * etm-trace 钩子实现 —— STM32H7B0（Cortex-M7，ETMv4）
 *
 * ⚠️ 本文件**不配置 ETM**，这是刻意的（见 trace_hooks.h 的说明）。
 *    外部工具（OpenOCD / 上位机网页）通过 SWD 完成：
 *      - TRCPRGCTLR / TRCCONFIGR / TRCVICTLR 等 ETMv4 配置
 *      - CSTF(S0) → ETF → TPIU 的 funnel 连接
 *      - PE2~PE6 的 AF0 复用与并行 TPIU 输出
 *
 * 这里只做：DWT 周期计数器 + 打点 GPIO + 诊断结构体（DTCM 固定地址）。
 */
#include "trace_hooks.h"
#include "stm32h7xx_hal.h"

#ifndef H7_CDC_PLL_R
#define H7_CDC_PLL_R 4
#endif

/* 固定到 DTCM 0x20003000：外部工具可以直接按地址读，不用查符号表。
 * （与 stm32h7b0_spi_trace 的 g_spi_trace 同一约定） */
__attribute__((section(".trace_diag"), used)) volatile trace_diag_t g_trace;

/* DWT / DEMCR（Cortex-M7） */
#define DEMCR      (*(volatile uint32_t *)0xE000EDFCu)
#define DWT_CTRL   (*(volatile uint32_t *)0xE0001000u)
#define DWT_CYCCNT (*(volatile uint32_t *)0xE0001004u)

/* 逻辑分析仪观察脚：PA0（推挽输出，每次打点翻转）。
 * 不接 LA 也不影响运行。 */
#define TRACE_PIN_PORT GPIOA
#define TRACE_PIN      GPIO_PIN_0

static uint32_t s_last_cycles;
static uint8_t s_pin_state;

extern uint32_t g_sysclk_hz;

void trace_hook_init(void)
{
    /* 显式初始化：NOLOAD 段的 C 静态初值不可靠，必须自己写一遍 */
    g_trace.magic = 0x34524354u; /* 'TRC4' */
    g_trace.sysclk_hz = 0;
    g_trace.ready = 0;
    g_trace.loops = 0;
    g_trace.marks = 0;
    g_trace.cyc_last = 0;
    g_trace.cyc_delta = 0;
    g_trace.usb_rx = 0;
    g_trace.usb_tx = 0;
    g_trace.etm_touched = 0;
    g_trace.hclk_hz = 0;
    g_trace.traceportck_hz = 0;
    g_trace.traceclk_hz = 0;
    g_trace.error = 0;
    s_last_cycles = 0;
    s_pin_state = 0;

    /* DWT 周期计数器 */
    DEMCR |= (1u << 24);   /* TRCENA */
    DWT->LAR = 0xC5ACCE55u; /* Cortex-M7 DWT software unlock */
    __DSB();
    DWT_CYCCNT = 0;
    DWT_CTRL |= (1u << 0); /* CYCCNTENA */

    /* 打点 GPIO：PA0 推挽输出 */
    {
        GPIO_InitTypeDef io = { 0 };
        __HAL_RCC_GPIOA_CLK_ENABLE();
        io.Pin = TRACE_PIN;
        io.Mode = GPIO_MODE_OUTPUT_PP;
        io.Pull = GPIO_NOPULL;
        io.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        HAL_GPIO_Init(TRACE_PIN_PORT, &io);
        HAL_GPIO_WritePin(TRACE_PIN_PORT, TRACE_PIN, GPIO_PIN_RESET);
    }
}

__attribute__((noinline)) void trace_hook_entry(void)
{
    /* 只放一个编译期可见的稳定落点，外部工具在这里下硬件断点。
     * 不要写成 static inline，否则会被优化掉。 */
    __asm volatile("nop");
}

__attribute__((noinline)) void trace_hook_ready(void)
{
    g_trace.sysclk_hz = g_sysclk_hz;
    g_trace.hclk_hz = HAL_RCC_GetHCLKFreq();
    g_trace.traceportck_hz = 560000000u / H7_CDC_PLL_R;
    g_trace.traceclk_hz = g_trace.traceportck_hz / 2u;
    g_trace.ready = 1u;
}

void trace_hook_mark(void)
{
    uint32_t now = DWT_CYCCNT;

    g_trace.marks++;
    g_trace.cyc_last = now;
    g_trace.cyc_delta = now - s_last_cycles;
    s_last_cycles = now;

    s_pin_state ^= 1u;
    HAL_GPIO_WritePin(TRACE_PIN_PORT, TRACE_PIN,
                      s_pin_state ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

