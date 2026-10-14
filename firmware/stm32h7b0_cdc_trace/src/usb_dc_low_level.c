/*
 * USB 低级初始化 + 中断分发表 —— STM32H7B0（USB1_OTG_HS，PA11/PA12，内嵌全速 PHY）
 *
 * ⚠️ 型号差异（踩过）：
 *   H743/H750 有**两个** USB 实例：USB_OTG_FS(0x40080000) 与 USB_OTG_HS(0x40040000)。
 *   H7B0/H7A3/H7B3 只有**一个**：`USB1_OTG_HS @ 0x40040000`，PA11/PA12 走内嵌全速 PHY，
 *   中断向量是 `OTG_HS_IRQn`。所以 H7B0 上不存在 USB_OTG_FS / USB_OTG_FS_PERIPH_BASE，
 *   复用 H743 源码时必须换掉这几处（本文件与 main.c、usb_glue_st_h7b0.c 已换好）。
 *
 * 与 F103 那份的差别：
 *   F103 是自写无参 `usb_dc_low_level_init(void)`、不编 HAL PCD；
 *   H7 这里沿用 CherryUSB 官方 DWC2 的**带参**接口 `usb_dc_low_level_init(uint8_t busid)`，
 *   内部调 HAL_PCD_MspInit（HAL 的 PCD 模块需要编译，已在 hal_conf 里打开）。
 */
#include "stm32h7xx_hal.h"
#include "usbd_core.h"
#include "usb_dwc2_param.h"

/* H7 两个 USB 口基址 */
#define USB_OTG_HS_BASE_ADDR 0x40040000UL

/* 中断分发（定义在 cherryusb/port/dwc2/usb_glue_st_h7b0.c，由那里的 IRQHandler 使用） */
extern void (*g_usb_dwc2_irq[2])(uint8_t busid);
extern uint8_t g_usb_dwc2_busid[2];

/* 板级钩子使用真实 PCD 句柄；USB 协议和 IRQ 均由 CherryUSB 管理。 */
static PCD_HandleTypeDef s_pcd;

void usb_dc_low_level_init(uint8_t busid)
{
    uint32_t base = (uint32_t)g_usbdev_bus[busid].reg_base;

    if (base == USB_OTG_HS_BASE_ADDR) {
        g_usb_dwc2_busid[1] = busid;
        g_usb_dwc2_irq[1] = USBD_IRQHandler;
    } else {
        g_usb_dwc2_busid[0] = busid;
        g_usb_dwc2_irq[0] = USBD_IRQHandler;
    }

    s_pcd.Instance = (USB_OTG_GlobalTypeDef *)base;
    HAL_PCD_MspInit(&s_pcd);
}

void usb_dc_low_level_deinit(uint8_t busid)
{
    uint32_t base = (uint32_t)g_usbdev_bus[busid].reg_base;

    s_pcd.Instance = (USB_OTG_GlobalTypeDef *)base;
    /* Disable the interrupt before clearing its dispatch function. */
    HAL_PCD_MspDeInit(&s_pcd);
    if (base == USB_OTG_HS_BASE_ADDR) {
        g_usb_dwc2_busid[1] = 0;
        g_usb_dwc2_irq[1] = NULL;
    } else {
        g_usb_dwc2_busid[0] = 0;
        g_usb_dwc2_irq[0] = NULL;
    }

}

/* ---------------- HAL_PCD_MspInit / DeInit（ST 惯例的板级钩子） ---------------- */
void HAL_PCD_MspInit(PCD_HandleTypeDef *hpcd)
{
    if (hpcd->Instance == USB_OTG_HS) {
        GPIO_InitTypeDef io = { 0 };

        /* PA11 = DM, PA12 = DP，AF10（H7B0 上是 OTG1_FS 复用；H743 上是 OTG_FS） */
        __HAL_RCC_GPIOA_CLK_ENABLE();
        io.Pin = GPIO_PIN_11 | GPIO_PIN_12;
        io.Mode = GPIO_MODE_AF_PP;
        io.Pull = GPIO_NOPULL;
        io.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
#ifdef GPIO_AF10_OTG1_FS
        io.Alternate = GPIO_AF10_OTG1_FS;
#else
        io.Alternate = GPIO_AF10_OTG_FS;
#endif
        HAL_GPIO_Init(GPIOA, &io);

        /* USB 内核时钟：H7 必须显式开，否则寄存器写不进去 */
        __HAL_RCC_USB1_OTG_HS_CLK_ENABLE();

        HAL_NVIC_SetPriority(OTG_HS_IRQn, 3, 0);
        HAL_NVIC_ClearPendingIRQ(OTG_HS_IRQn);
        HAL_NVIC_EnableIRQ(OTG_HS_IRQn);
    }
    /* H743 上如果改用 FS 口（0x40080000），把下面这段打开并把 base 换成 OTG_FS */
#if 0
    else if (hpcd->Instance == USB_OTG_FS) {
        GPIO_InitTypeDef io = { 0 };
        __HAL_RCC_GPIOA_CLK_ENABLE();
        io.Pin = GPIO_PIN_11 | GPIO_PIN_12;
        io.Mode = GPIO_MODE_AF_PP;
        io.Pull = GPIO_NOPULL;
        io.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        io.Alternate = GPIO_AF10_OTG_FS;
        HAL_GPIO_Init(GPIOA, &io);
        __HAL_RCC_USB_OTG_FS_CLK_ENABLE();
        HAL_NVIC_SetPriority(OTG_FS_IRQn, 3, 0);
        HAL_NVIC_EnableIRQ(OTG_FS_IRQn);
    }
#endif
}

void HAL_PCD_MspDeInit(PCD_HandleTypeDef *hpcd)
{
    if (hpcd->Instance == USB_OTG_HS) {
        HAL_NVIC_DisableIRQ(OTG_HS_IRQn);
        HAL_NVIC_ClearPendingIRQ(OTG_HS_IRQn);
        __HAL_RCC_USB1_OTG_HS_CLK_DISABLE();
        HAL_GPIO_DeInit(GPIOA, GPIO_PIN_11 | GPIO_PIN_12);
    }
}
