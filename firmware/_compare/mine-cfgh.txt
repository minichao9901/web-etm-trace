/*
 * CherryUSB 配置 —— STM32F103 + 老款 USB FS device 外设（fsdev port）
 *
 * 这个文件是 CherryUSB 唯一需要按芯片裁剪的入口：官方模板见
 * cherryusb_config_template.h，这里只保留 device / cdc_acm 需要的最小集合。
 */
#ifndef CHERRYUSB_CONFIG_H
#define CHERRYUSB_CONFIG_H

/* ================ USB common Configuration ================ */

#include <stdio.h>
#include "trace_hooks.h"

/* 调试输出走 SEGGER RTT（trace_hooks.h 里的 trace_log_printf） */
#define CONFIG_USB_PRINTF(...) trace_log_printf(__VA_ARGS__)

/* USB_DBG_INFO：只保留 LOG_INFO/WRN/ERR/RAW；fsdev 不允许开到 USB_DBG_LOG */
#ifndef CONFIG_USB_DBG_LEVEL
#define CONFIG_USB_DBG_LEVEL USB_DBG_INFO
#endif

/* 不带颜色，RTT 里看彩色转义码没意义 */
// #define CONFIG_USB_PRINTF_COLOR_ENABLE

/* 不用 dcache（Cortex-M3 没有 D-Cache），对齐 4 字节 */
#define CONFIG_USB_ALIGN_SIZE 4

/* 不用 DMA、也不要求无缓存段，这里直接放普通 .bss，省得改链接脚本 */
#define USB_NOCACHE_RAM_SECTION
#ifndef USB_MEM_ALIGNX
#define USB_MEM_ALIGNX __attribute__((aligned(CONFIG_USB_ALIGN_SIZE)))
#endif

/* ================= USB Device Stack Configuration ================ */

/* EP0 收发缓冲，512 是通用值；F103 只有 20KB RAM，够用 */
#ifndef CONFIG_USBDEV_REQUEST_BUFFER_LEN
#define CONFIG_USBDEV_REQUEST_BUFFER_LEN 512
#endif

/* 用 CherryUSB 自己的 usb_memcpy，避开 newlib memcpy 非 4 字节对齐的坑 */
// #define CONFIG_USB_MEMCPY_DISABLE

/* 高级描述符接口（回调式），cdc_acm_template.c 用的就是这套 */
#define CONFIG_USBDEV_ADVANCE_DESC

/* EP0 setup 留在中断里处理（裸机 + 无 RTOS） */
// #define CONFIG_USBDEV_EP0_THREAD

#define CONFIG_USBDEV_MAX_BUS 1

/* ================ USB Device Port Configuration ================*/

/* ---------------- FSDEV Configuration ---------------- */
/* STM32F1 的 PMA 必须按 16 位访问：PMA_ACCESS=2
 * （usb_glue_st.c 里有 #if CONFIG_USBDEV_FSDEV_PMA_ACCESS != PMA_ACCESS #error 的自检） */
#define CONFIG_USBDEV_FSDEV_PMA_ACCESS 2

/* F103 的 USB 专用 RAM 是 512 字节 */
#define CONFIG_USB_FSDEV_RAM_SIZE 512

/* 不用 host 栈，这里全部不启用；若将来要 host，需要补 usb_hc_fsdev/host 相关配置 */

#endif /* CHERRYUSB_CONFIG_H */
