/*
 * STM32F1xx HAL 配置 —— 只开这个 CDC ACM demo 真正用得到的模块
 *
 * 与 CubeMX 生成的版本相比：去掉了 ADC/CAN/I2C/SPI/UART/TIM/RTC/SD/... 的开关，
 * 保留 HAL + RCC + GPIO + CORTEX + PWR + FLASH + PCD。
 *
 * ⚠️ HAL_PCD_MODULE_ENABLED 是 **必需的**：CherryUSB 的 usb_dc_fsdev.c 用到了
 *    stm32f1xx_hal_pcd.h 里的 PCD_xxx 宏（该头文件只在定义了此宏时才被包含）。
 *    但我们**不编译** stm32f1xx_hal_pcd.c —— USB 状态机由 CherryUSB 自己实现。
 */
#ifndef __STM32F1xx_HAL_CONF_H
#define __STM32F1xx_HAL_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

/* ########################## Module Selection ############################## */
#define HAL_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_EXTI_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_PCD_MODULE_ENABLED

/* ########################## Oscillator Values ############################# */
/**
 * @brief Adjust the value of External High Speed oscillator (HSE) used in your application.
 *        This value is used by the RCC HAL module to compute the system frequency
 *        (when HSE is used as system clock source, directly or through the PLL).
 */
#if !defined(HSE_VALUE)
#define HSE_VALUE 8000000U /*!< Value of the External oscillator in Hz */
#endif /* HSE_VALUE */

#if !defined(HSE_STARTUP_TIMEOUT)
#define HSE_STARTUP_TIMEOUT 100U /*!< Time out for HSE start up, in ms */
#endif /* HSE_STARTUP_TIMEOUT */

#if !defined(HSI_VALUE)
#define HSI_VALUE 8000000U /*!< Value of the Internal oscillator in Hz */
#endif /* HSI_VALUE */

#if !defined(LSI_VALUE)
#define LSI_VALUE 40000U /*!< LSI Typical Value in Hz */
#endif /* LSI_VALUE */

#if !defined(LSE_VALUE)
#define LSE_VALUE 32768U /*!< Value of the External Low Speed oscillator in Hz */
#endif /* LSE_VALUE */

#if !defined(LSE_STARTUP_TIMEOUT)
#define LSE_STARTUP_TIMEOUT 5000U /*!< Time out for LSE start up, in ms */
#endif /* LSE_STARTUP_TIMEOUT */

/* ########################### System Configuration ######################### */
#define VDD_VALUE 3300U            /*!< Value of VDD in mv */
#define TICK_INT_PRIORITY 0x00U    /*!< tick interrupt priority */
#define USE_RTOS 0U
#define PREFETCH_ENABLE 1U

#define USE_HAL_ADC_REGISTER_CALLBACKS 0U
#define USE_HAL_CAN_REGISTER_CALLBACKS 0U
#define USE_HAL_CEC_REGISTER_CALLBACKS 0U
#define USE_HAL_DAC_REGISTER_CALLBACKS 0U
#define USE_HAL_ETH_REGISTER_CALLBACKS 0U
#define USE_HAL_HCD_REGISTER_CALLBACKS 0U
#define USE_HAL_I2C_REGISTER_CALLBACKS 0U
#define USE_HAL_I2S_REGISTER_CALLBACKS 0U
#define USE_HAL_MMC_REGISTER_CALLBACKS 0U
#define USE_HAL_NAND_REGISTER_CALLBACKS 0U
#define USE_HAL_NOR_REGISTER_CALLBACKS 0U
#define USE_HAL_PCCARD_REGISTER_CALLBACKS 0U
#define USE_HAL_PCD_REGISTER_CALLBACKS 0U
#define USE_HAL_RTC_REGISTER_CALLBACKS 0U
#define USE_HAL_SD_REGISTER_CALLBACKS 0U
#define USE_HAL_SMARTCARD_REGISTER_CALLBACKS 0U
#define USE_HAL_IRDA_REGISTER_CALLBACKS 0U
#define USE_HAL_SRAM_REGISTER_CALLBACKS 0U
#define USE_HAL_SPI_REGISTER_CALLBACKS 0U
#define USE_HAL_TIM_REGISTER_CALLBACKS 0U
#define USE_HAL_UART_REGISTER_CALLBACKS 0U
#define USE_HAL_USART_REGISTER_CALLBACKS 0U
#define USE_HAL_WWDG_REGISTER_CALLBACKS 0U

/* ########################## Assert Selection ############################## */
/* #define USE_FULL_ASSERT    1U */

/* Includes ------------------------------------------------------------------*/
#ifdef HAL_RCC_MODULE_ENABLED
#include "stm32f1xx_hal_rcc.h"
#endif

#ifdef HAL_GPIO_MODULE_ENABLED
#include "stm32f1xx_hal_gpio.h"
#endif

#ifdef HAL_EXTI_MODULE_ENABLED
#include "stm32f1xx_hal_exti.h"
#endif

#ifdef HAL_DMA_MODULE_ENABLED
#include "stm32f1xx_hal_dma.h"
#endif

#ifdef HAL_CORTEX_MODULE_ENABLED
#include "stm32f1xx_hal_cortex.h"
#endif

#ifdef HAL_PWR_MODULE_ENABLED
#include "stm32f1xx_hal_pwr.h"
#endif

#ifdef HAL_FLASH_MODULE_ENABLED
#include "stm32f1xx_hal_flash.h"
#endif

#ifdef HAL_PCD_MODULE_ENABLED
#include "stm32f1xx_hal_pcd.h"
#endif

/* Exported macro ------------------------------------------------------------*/
#ifdef USE_FULL_ASSERT
/**
 * @brief  The assert_param macro is used for function's parameters check.
 */
#define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
void assert_failed(uint8_t *file, uint32_t line);
#else
#define assert_param(expr) ((void)0U)
#endif /* USE_FULL_ASSERT */

#ifdef __cplusplus
}
#endif

#endif /* __STM32F1xx_HAL_CONF_H */
