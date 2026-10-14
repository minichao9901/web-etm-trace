/*
 * STM32F103 时钟初始化：SYSCLK 72 MHz，USB 时钟 48 MHz
 *
 * 硬约束（F1 的 USB 只能吃 48MHz，且来源只有 PLL/1.5）：
 *   SYSCLK = HSE(8MHz) × PLLMUL(9) = 72 MHz
 *   USB    = SYSCLK / 1.5 = 48 MHz   ← 必须先切到 72MHz 才能设这个分频
 * 所以本文件对 HSE 的要求是 **8 MHz**：
 *   - F103ZE 板（目标板）若用 8MHz 晶振：直接可用；
 *   - 若换 12MHz 晶振：改了 HSE_VALUE 也起不来 USB（12×9/1.5=72，仍要 PLLMUL 满足
 *     SYSCLK/1.5==48 ⇒ SYSCLK==72 ⇒ PLLMUL=6，但 USB 预分频还必须等于 1.5，
 *     而 12/1.5=8，只有 8×9=72 这条组合能同时满足，12MHz 只能靠 HSE/2 后走别的分频，
 *     F1 不支持），结论：**F1 做 USB 必须 8 MHz 晶振**。
 *
 * 晶振频率由构建脚本传入 -DHSE_VALUE=xxxxxxx，默认 8000000。
 */
#include "stm32f1xx_hal.h"
#include "stm32f1xx.h"

#ifndef HSE_VALUE
#define HSE_VALUE 8000000U
#endif

uint32_t g_sysclk_hz = 0;
uint32_t g_clock_err = 0;

void system_clock_init(void)
{
    RCC_OscInitTypeDef osc = { 0 };
    RCC_ClkInitTypeDef clk = { 0 };
    RCC_PeriphCLKInitTypeDef periph = { 0 };

    /*
     * PLLMUL 的选择：让 SYSCLK 尽量到 72MHz。
     *   8MHz  → ×9  = 72MHz   （USB 48MHz ✓）
     *   12MHz → ×6  = 72MHz   （USB 预分频仍是 /1.5 → 48MHz ✓，F1 也支持）
     * 其它频率就退回 HSE 直连（USB 会不可用，但固件不死）。
     */
#if (HSE_VALUE == 8000000U)
    uint32_t pll_mul = RCC_PLL_MUL9;
#elif (HSE_VALUE == 12000000U)
    uint32_t pll_mul = RCC_PLL_MUL6;
#else
    uint32_t pll_mul = 0;
#endif

    if (pll_mul != 0) {
        /* 1) 打开 HSE + PLL（PLL 源 = HSE） */
        osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
        osc.HSEState = RCC_HSE_ON;
        osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
        osc.HSIState = RCC_HSI_ON;
        osc.PLL.PLLState = RCC_PLL_ON;
        osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
        osc.PLL.PLLMUL = pll_mul;
        if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
            g_clock_err |= 1u; /* HSE 或 PLL 起不来 */
        }

        /* 2) 切换 SYSCLK 到 PLL：72MHz / APB1 36MHz / APB2 72MHz */
        if (g_clock_err == 0u) {
            clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                            RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
            clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
            clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
            clk.APB1CLKDivider = RCC_HCLK_DIV2;
            clk.APB2CLKDivider = RCC_HCLK_DIV1;
            if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK) {
                g_clock_err |= 2u;
            }
        }
    } else {
        /* 非 8M/12M 晶振：只用 HSI，固件照跑，USB 不可用 */
        osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
        osc.HSIState = RCC_HSI_ON;
        osc.PLL.PLLState = RCC_PLL_NONE;
        (void)HAL_RCC_OscConfig(&osc);
        g_clock_err |= 4u;
    }

    /* 3) USB 时钟 = PLL / 1.5 = 48MHz（必须在 SYSCLK 已经稳定之后） */
    if (g_clock_err == 0u) {
        periph.PeriphClockSelection = RCC_PERIPHCLK_USB;
        periph.UsbClockSelection = RCC_USBCLKSOURCE_PLL_DIV1_5;
        if (HAL_RCCEx_PeriphCLKConfig(&periph) != HAL_OK) {
            g_clock_err |= 8u;
        }
    }

    /* 4) 记录实际时钟，供 trace 侧读 */
    g_sysclk_hz = HAL_RCC_GetHCLKFreq();
}
