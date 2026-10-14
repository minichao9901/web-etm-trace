/*
 * CherryUSB 配置 —— STM32H7B0（dwc2 port, USB1_OTG_HS, PA11/PA12, 全速）
 *
 * 与 F103 那份的差别：
 *   - port 换 dwc2（H7 是 Synopsys DWC2 OTG，不是 F1 的老 USB FS device）
 *   - USB 时钟由 25MHz HSE + PLL3Q 生成 48MHz。
 *   - 当前使用 FIFO/中断模式，不启用 DMA；不能按引脚名称判断 DMA 能力。
 *   - D-Cache 关闭。.noncacheable 只做段分组，不自动改变 MPU 缓存属性。
 */
#ifndef CHERRYUSB_CONFIG_H
#define CHERRYUSB_CONFIG_H

/* ================ USB common Configuration ================ */

#include <stdio.h>
#include "trace_hooks.h"

#define CONFIG_USB_PRINTF(...) trace_log_printf(__VA_ARGS__)

#ifndef CONFIG_USB_DBG_LEVEL
#define CONFIG_USB_DBG_LEVEL USB_DBG_INFO
#endif

/* 对齐按 D-Cache 行（H7 的 cache line = 32 字节），缓冲放 .noncacheable */
#define CONFIG_USB_DCACHE_ENABLE
#define CONFIG_USB_ALIGN_SIZE 32

#define USB_NOCACHE_RAM_SECTION __attribute__((section(".noncacheable"), aligned(CONFIG_USB_ALIGN_SIZE)))
#ifndef USB_MEM_ALIGNX
#define USB_MEM_ALIGNX __attribute__((aligned(CONFIG_USB_ALIGN_SIZE)))
#endif

/* ================= USB Device Stack Configuration ================ */

#ifndef CONFIG_USBDEV_REQUEST_BUFFER_LEN
#define CONFIG_USBDEV_REQUEST_BUFFER_LEN 512
#endif

#define CONFIG_USBDEV_ADVANCE_DESC
#define CONFIG_USBDEV_MAX_BUS 1

/* ================ USB Device Port Configuration ================*/

/* ---------------- DWC2 Configuration ---------------- */
/* 当前 FIFO 模式；启用 DMA 需要重新验证 DWC2 参数、内存可达性和缓存策略。 */
// #define CONFIG_USB_DWC2_DMA_ENABLE

/* ⚠️ 也别定义 CONFIG_USB_HS：本工程走内部 FS PHY（全速，12Mbps） */
// #define CONFIG_USB_HS

#endif /* CHERRYUSB_CONFIG_H */
