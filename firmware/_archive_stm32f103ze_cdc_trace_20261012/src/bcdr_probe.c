/*
 * USB BCDR 写入实验（临时诊断用）
 *
 * 现象：usb_dc_init() 里 `USB->BCDR |= DPPU`（反汇编确认是 strh r2,[r3,#0x58]）执行过，
 *       但 SWD 回读 0x40005C58 永远是 0 —— 需要区分三种可能：
 *         A. 写根本没发生（调用链没走到）
 *         B. 写发生了，但被后续 USB 中断处理清掉
 *         C. 写发生了，但 F1 的 BCDR 需要额外条件才生效
 *
 * 本函数用"写两次 + 中间长延时 + 全程记录"来区分：
 *   bcdr_pre   —— 第一次写之前的值
 *   bcdr_set   —— 写完之后立刻回读的值（B 成立的话这里应该是 0x8000）
 *   bcdr_after —— 延时之后再回读（若 set 有值而 after 为 0，则说明被清掉了）
 */
#include <stdint.h>

#define USB_CNTR (*(volatile uint16_t *)0x40005C40u)
#define USB_ISTR (*(volatile uint16_t *)0x40005C44u)
#define USB_BCDR (*(volatile uint16_t *)0x40005C58u)

volatile uint16_t g_bcdr_pre;
volatile uint16_t g_bcdr_set;
volatile uint16_t g_bcdr_after;
volatile uint16_t g_bcdr_after2;

void bcdr_experiment(void)
{
    volatile uint32_t i;

    g_bcdr_pre = USB_BCDR;

    /* 完全照抄 usb_dc_init 的收尾动作，只多记录回读值 */
    USB_CNTR = (uint16_t)0xBD00u; /* CTRM|WKUPM|SUSPM|ERRM|ESOFM|RESETM */
    USB_BCDR |= (uint16_t)0x8000u; /* DPPU */
    g_bcdr_set = USB_BCDR;

    /* 长延时（约 200ms @72MHz），期间主循环不跑、中断照常 */
    for (i = 0; i < 200000u; i++) {
        __asm volatile("nop");
    }
    g_bcdr_after = USB_BCDR;

    for (i = 0; i < 200000u; i++) {
        __asm volatile("nop");
    }
    g_bcdr_after2 = USB_BCDR;
}
