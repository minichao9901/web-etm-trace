/*
 * USB 低级初始化 —— STM32F103（fsdev port）
 *
 * 完全照抄参照工程 cherry_drag_n_drop_bootloader_fat16 的 HAL_PCD_MspInit
 * （E:\Share\github\stm32f103_cherryusb_drag_n_drop_iap\...\Src\stm32f1xx_hal_msp.c:562）：
 *
 *   __HAL_RCC_USB_CLK_ENABLE();
 *   HAL_NVIC_SetPriority(USB_HP_CAN1_TX_IRQn,  0, 0);  HAL_NVIC_EnableIRQ(USB_HP_CAN1_TX_IRQn);
 *   HAL_NVIC_SetPriority(USB_LP_CAN1_RX0_IRQn, 0, 0);  HAL_NVIC_EnableIRQ(USB_LP_CAN1_RX0_IRQn);
 *
 * ⚠️ **两条中断都必须开**（这是我之前唯一漏掉的东西）：
 *    STM32F1 的 USB 外设把中断拆成两条向量 ——
 *      USB_HP_CAN1_TX  ：双缓冲批量/同步端点的传输完成
 *      USB_LP_CAN1_RX0 ：CTR / RESET / SUSP / WKUP / ERR 等其余全部
 *    如果只开 LP，而硬件把某个事件报到 HP 上，向量表里 HP 指向 startup 文件里的
 *    Default_Handler（死循环），枚举就会卡在"取设备描述符"这一步，表现为
 *    Windows 报「未知 USB 设备(设备描述符请求失败) Code 43」。
 *
 * ⚠️ 另外：**F1 的经典 USB FS device 外设没有 BCDR 寄存器**（RM0008 寄存器表只到
 *    BTABLE@0x50；CubeF1 里连 BCDR/DPPU 这两个名字都不存在）。D+ 内部上拉是常态接通的，
 *    没有可以软件开关的 DPPU 位。所以：
 *      - 不要去写 BCDR（CherryUSB 的 usb_dc_init 里有这行，在 F1 上写到空地址，无害）；
 *      - 想"软拔插"只能靠 CNTR 的 FRES/PDWN 位（见 tools/usb_softreset_f103.py）。
 */
#include "stm32f1xx_hal.h"
#include "usbd_core.h"

void usb_dc_low_level_init(void)
{
    /* 1) USB 外设时钟：APB1 */
    __HAL_RCC_USB_CLK_ENABLE();

    /* 2) 清 USB 唤醒中断（EXTI 线 18）挂起标志：
     *    __HAL_GPIO_EXTI_CLEAR_IT 只接受 GPIO_PIN_x 宏，没有 bit18，直接写寄存器 */
    EXTI->PR = (1u << 18);

    /* 3) 两条 USB 中断都要使能（理由见文件头）。优先级与参照工程一致：都为 0 */
    HAL_NVIC_SetPriority(USB_HP_CAN1_TX_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(USB_HP_CAN1_TX_IRQn);

    HAL_NVIC_SetPriority(USB_LP_CAN1_RX0_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(USB_LP_CAN1_RX0_IRQn);
}

void usb_dc_low_level_deinit(void)
{
    HAL_NVIC_DisableIRQ(USB_HP_CAN1_TX_IRQn);
    HAL_NVIC_DisableIRQ(USB_LP_CAN1_RX0_IRQn);
    __HAL_RCC_USB_CLK_DISABLE();
}
