/*
 * 启动阶段探针（boot stage probe）—— 给"卡在哪一步"提供硬证据
 *
 * 现象驱动的调试手段：当 SWD 读回来的 RAM 不可信（CPU 在跑、探针在重连）时，
 * 用一根 GPIO 的脉冲宽度就能确定启动停在了哪一步 —— 逻辑分析仪或 SWD 读 g_boot_stage 都行。
 *
 * 引脚：PA1（推挽输出）。每调一次 probe_stage(n)：
 *   PA1 拉高 -> 延时约 20us（不用时钟，纯 NOP 循环）-> 拉低 -> 记录 g_boot_stage = n
 * 用 LA 看脉冲个数 = 已经走过几步；用 SWD 读 g_boot_stage 看最后停在第几步。
 *
 * ⚠️ 这段代码不依赖 HAL、不依赖时钟配置，尽量早调用，越早越有价值。
 */
#include <stdint.h>

#define PROBE_GPIOA_CRL (*(volatile uint32_t *)0x40010800u)
#define PROBE_GPIOA_BSRR (*(volatile uint32_t *)0x40010810u)
#define PROBE_GPIOA_BRR (*(volatile uint32_t *)0x40010814u)
#define PROBE_RCC_APB2ENR (*(volatile uint32_t *)0x40021018u)

/* 固定地址 + 不被优化掉：用 nm 查符号即可 */
__attribute__((used)) volatile uint32_t g_boot_stage = 0xFFFFFFFFu;
__attribute__((used)) volatile uint32_t g_boot_stage_prev = 0xFFFFFFFFu;

void probe_gpio_init(void)
{
    PROBE_RCC_APB2ENR |= (1u << 2); /* IOPAEN */
    (void)PROBE_RCC_APB2ENR;
    {
        uint32_t crl = PROBE_GPIOA_CRL;
        crl &= ~(0xFu << 4);        /* PA1 = bits[7:4] */
        crl |= (0x1u << 4);         /* 通用推挽输出，2MHz */
        PROBE_GPIOA_CRL = crl;
    }
    PROBE_GPIOA_BRR = (1u << 1);
}

void probe_stage(uint32_t stage)
{
    volatile uint32_t i;

    g_boot_stage_prev = g_boot_stage;
    g_boot_stage = stage;
    probe_gpio_init(); /* 幂等，确保 GPIO 已配好 */

    PROBE_GPIOA_BSRR = (1u << 1);
    for (i = 0; i < 400u; i++) {
        __asm volatile("nop");
    }
    PROBE_GPIOA_BRR = (1u << 1);
}
