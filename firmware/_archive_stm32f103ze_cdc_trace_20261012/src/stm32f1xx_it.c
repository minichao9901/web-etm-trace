/*
 * 中断向量实现 —— STM32F103 + CherryUSB(fsdev)
 *
 * 需要实现三条：
 *   SysTick_Handler            HAL 的 1ms 时基。
 *                              ⚠️ 踩过的坑：没写这条时 ST 启动文件把它弱别名到 Default_Handler，
 *                              而 HAL_Init() 会开 SysTick → 第一个 1ms 中断就把 CPU 困死在那里
 *                              （现象：USB 完全不枚举、g_trace 一直是 0）。
 *   USB_LP_CAN1_RX0_IRQHandler USB 低优先级：CTR / RESET / SUSP / WKUP / ERR
 *   USB_HP_CAN1_TX_IRQHandler  USB 高优先级：双缓冲批量/同步端点的传输完成
 *
 * ⚠️ 两条 USB 中断**都要实现**（参照工程 stm32f1xx_hal_msp.c 里两条 NVIC 都使能）。
 *    只实现 LP、让 HP 落到 Default_Handler，是"枚举卡在取设备描述符"的一个典型原因。
 *
 * ⚠️ 不要把 HAL 的 HAL_PCD_IRQHandler 挂这里 —— 本工程没编译 HAL PCD 状态机，
 *    USB 全部由 CherryUSB 的 USBD_IRQHandler(busid) 处理。
 */
#include "stm32f1xx_hal.h"
#include "usbd_core.h"
#include "trace_hooks.h"

/* F1 的 USB 中断状态寄存器（0x44）—— 直接用地址访问：
 * ST 的 stm32f103xb.h 与 CherryUSB 的 usb_fsdev_reg.h 有同名的 USB_TypeDef，
 * 用结构体名会撞类型。ISTR 是只读的，读它不会清标志。 */
#define F1_USB_ISTR (*(volatile uint16_t *)0x40005C44u)

void SysTick_Handler(void)
{
    HAL_IncTick();
}

static inline void usb_isr_count(void)
{
    uint16_t istr = F1_USB_ISTR;

    g_trace.usb_irq++;
    if (istr & 0x8000u) {
        g_trace.usb_ctr++; /* CTR：有端点传输完成（SETUP 也算） */
    }
    if (istr & 0x0400u) {
        g_trace.usb_reset++; /* RESET */
    }
    if (istr & 0x2000u) {
        g_trace.usb_err++; /* ERR */
    }
    if (istr & 0x0800u) {
        g_trace.usb_susp++; /* SUSP */
    }
}

/*
 * ⚠️⚠️ 为什么这两个函数**故意改名字**（f1_usb_lp_handler / f1_usb_hp_handler）：
 *
 * ST 的 startup_stm32f103xb.s 里对每个中断都写了
 *     .weak  USB_LP_CAN1_RX0_IRQHandler
 *     .thumb_set USB_LP_CAN1_RX0_IRQHandler,Default_Handler
 * 这个组合会**把符号在 startup.o 内部绑定死**：即使我们在别的目标文件里提供强定义，
 * 链接器依然让向量表指向 Default_Handler（实测：vector[19]/[20] 都是 Default_Handler 的地址，
 * 而 nm 里确实存在我们那个 T 强符号；注掉 .weak 就立刻变成未定义 U 并可正常解析）。
 * 后果极具迷惑性：USB 中断永远进不到我们的代码 → 枚举卡死 → Windows 报
 * 「未知 USB 设备(设备描述符请求失败) Code 43」。
 *
 * 解决办法（不改 ST 文件、地址无关）：我们定义**新名字**的强符号，再用链接器
 * `--defsym=USB_LP_CAN1_RX0_IRQHandler=f1_usb_lp_handler` 把向量表里的名字重定向过来。
 * 见 build.ps1 的链接段。SysTick_Handler 名字正常是因为它本来就没被冲突（同样受益于 defsym
 * 方案的统一性，这里也一并重定向了一份，保持风格一致）。
 */
void f1_usb_lp_handler(void)
{
    usb_isr_count();
    USBD_IRQHandler(0);
}

void f1_usb_hp_handler(void)
{
    usb_isr_count();
    USBD_IRQHandler(0);
}

/* Cortex-M3 的异常处理：出错停在原地，便于 SWD 抓现场 */
void HardFault_Handler(void)
{
    __asm volatile("bkpt #0");
    for (;;) {
    }
}

void NMI_Handler(void)
{
    for (;;) {
    }
}

void MemManage_Handler(void)
{
    for (;;) {
    }
}

void BusFault_Handler(void)
{
    for (;;) {
    }
}

void UsageFault_Handler(void)
{
    for (;;) {
    }
}

void SVC_Handler(void)
{
    for (;;) {
    }
}

void DebugMon_Handler(void)
{
    for (;;) {
    }
}

void PendSV_Handler(void)
{
    for (;;) {
    }
}
