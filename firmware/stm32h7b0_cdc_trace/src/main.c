/*
 * STM32H7B0 + CherryUSB CDC ACM 例程（etm-trace 目标固件）
 * ============================================================================
 * 目录定位：E:\web-etm-trace\firmware\stm32h7b0_cdc_trace
 *
 * 两个目的：
 *   1) 把 CherryUSB 1.6.1 的 demo/cdc_acm_template.c 跑到 STM32H7B0 上：
 *      USB1_OTG_HS（PA11/PA12，内部全速 PHY），USB 时钟为 HSE 派生的 PLL3Q 48MHz。
 *      port 用 dwc2（H7 是 Synopsys DWC2 IP，不是 F1 的老 USB FS device）。
 *   2) 作为 **etm-trace 的被测目标**：只提供稳定入口 + 就绪标志 + DWT 打点，
 *      **固件不配置、不开启 ETM** —— ETMv4/CSTF/ETF/TPIU 与 PE2~PE6 全部交给
 *      外部 SWD 工具（见 src/trace_hooks.h）。
 *
 * 时钟（与 stm32h7b0_spi_trace 同源，HSE 25MHz）：
 *   PLL1: M=5, N=112, P=2, Q=3, R=4
 *         VCO = 560MHz -> SYSCLK = VCO/P，TRACEPORTCK = VCO/R，物理 TRACECLK = VCO/R/2
 *   AXI/HCLK = /2 = 140MHz，APB1/2/3 = /2 = 70MHz，APB4 = /4 = 35MHz
 *   PLL3: M=5, N=48, Q=5 -> USB 48MHz（不随 CPU / Trace 档位改变）
 *
 * CDC 命令（任意串口工具 / Python 脚本）：
 *   PING | INFO | MARK | ECHO off | ECHO on | ECHO <text>
 * 默认"收到什么回什么"，回环测试直接可用。
 *
 * 构建：pwsh -NoProfile -File .\build.ps1
 */
#include "stm32h7xx_hal.h"
#include "usbd_core.h"
#include "usbd_cdc_acm.h"
#include "trace_hooks.h"
#include "SEGGER_RTT.h"
#include "cdc_console.h"

#include <stdarg.h>
#include <string.h>

#ifndef H7_CDC_PLL_P
#define H7_CDC_PLL_P 2
#endif
#ifndef H7_CDC_PLL_R
#define H7_CDC_PLL_R 4
#endif

/* 默认板级接法：VDD33USB / VDD50USB 外部提供 3.3V。
 * 若 VDD50USB 接 5V、VDD33USB 使用内部稳压器，构建时指定 -UsbSupply Internal。 */
#ifndef H7_CDC_USB_INTERNAL_REGULATOR
#define H7_CDC_USB_INTERNAL_REGULATOR 0
#endif

/* 由本文件提供，供 trace_hooks.c 读 */
uint32_t g_sysclk_hz = 0;
uint32_t g_clock_err = 0;

extern void probe_stage(uint32_t stage);

/* ======================= CherryUSB CDC ACM 描述符 ======================= */
#define CDC_IN_EP  0x81
#define CDC_OUT_EP 0x02
#define CDC_INT_EP 0x83

#define USBD_VID           0xF055
#define USBD_PID           0x7B01
#define USBD_MAX_POWER     100
#define USBD_LANGID_STRING 1033

#define USB_CONFIG_SIZE (9 + CDC_ACM_DESCRIPTOR_LEN)

/* 全速 → 64 字节包 */
#define CDC_MAX_MPS 64
#define CDC_RX_SIZE CDC_MAX_MPS
#define CDC_TX_SIZE 2048u

static const uint8_t device_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0xEF, 0x02, 0x01, USBD_VID, USBD_PID, 0x0100, 0x01)
};

static const uint8_t config_descriptor[] = {
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x02, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    CDC_ACM_DESCRIPTOR_INIT(0x00, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, CDC_MAX_MPS, 0x02)
};

static const char *string_descriptors[] = {
    (const char[]){ 0x09, 0x04 },
    "web-etm-trace",
    "STM32H7B0 CDC ACM (CherryUSB)",
    "H7B0CDC0001",
};

static const uint8_t *device_descriptor_callback(uint8_t speed)
{
    (void)speed;
    return device_descriptor;
}

static const uint8_t *config_descriptor_callback(uint8_t speed)
{
    (void)speed;
    return config_descriptor;
}

static const char *string_descriptor_callback(uint8_t speed, uint8_t index)
{
    (void)speed;
    if (index >= (sizeof(string_descriptors) / sizeof(char *))) {
        return NULL;
    }
    return string_descriptors[index];
}

static const struct usb_descriptor cdc_descriptor = {
    .device_descriptor_callback = device_descriptor_callback,
    .config_descriptor_callback = config_descriptor_callback,
    .device_quality_descriptor_callback = NULL, /* FS-only device */
    .string_descriptor_callback = string_descriptor_callback
};

/* D-Cache 关闭；段名本身不改变 MPU 属性。USB 缓冲仍按 32 字节对齐。 */
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t read_buffer[CDC_RX_SIZE];
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t write_buffer[CDC_TX_SIZE];

static volatile uint32_t rx_len;
static uint32_t rx_processed;
static volatile uint32_t rx_total;
static volatile uint32_t tx_total;
static volatile uint8_t ep_tx_busy_flag;
static volatile uint8_t dtr_enable;
static volatile uint8_t usb_configured;
static volatile uint8_t rx_pending;
static uint8_t rx_armed;
static uint8_t banner_sent;

static const char banner[] =
    "\r\n=== STM32H7B0 CherryUSB CDC ACM (etm-trace target) ===\r\n"
    "commands: PING | INFO | MARK | ECHO on|off | ECHO <text>\r\n";

/* ======================= 事件回调 ======================= */
static void cdc_session_reset(void)
{
    usb_configured = 0;
    dtr_enable = 0;
    rx_pending = rx_armed = 0;
    rx_len = rx_processed = 0;
    ep_tx_busy_flag = banner_sent = 0;
    cdc_console_reset();
}

static void usbd_event_handler(uint8_t busid, uint8_t event)
{
    (void)busid;
    switch (event) {
        case USBD_EVENT_RESET:
            cdc_session_reset();
            USB_LOG_RAW("[usb] reset\r\n");
            break;
        case USBD_EVENT_CONNECTED:
            USB_LOG_RAW("[usb] connected\r\n");
            break;
        case USBD_EVENT_DISCONNECTED:
            cdc_session_reset();
            USB_LOG_RAW("[usb] disconnected\r\n");
            break;
        case USBD_EVENT_RESUME:
        case USBD_EVENT_SUSPEND:
            break;
        case USBD_EVENT_CONFIGURED:
            cdc_session_reset();
            usb_configured = 1;
            USB_LOG_RAW("[usb] configured\r\n");
            break;
        case USBD_EVENT_SET_REMOTE_WAKEUP:
        case USBD_EVENT_CLR_REMOTE_WAKEUP:
            break;
        default:
            break;
    }
}

void usbd_cdc_acm_bulk_out(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)busid;
    (void)ep;
    rx_armed = 0;
    rx_len = nbytes;
    rx_processed = 0;
    rx_pending = 1;
    rx_total += nbytes;
    g_trace.usb_rx = rx_total;
    /* 主循环处理完这包才重新接收；TX 满时通过 OUT NAK 施加背压。 */
}

void usbd_cdc_acm_bulk_in(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    tx_total += nbytes;
    g_trace.usb_tx = tx_total;

    uint16_t mps = usbd_get_ep_mps(busid, ep);
    if (usb_configured && mps && nbytes && (nbytes % mps) == 0) {
        /* Keep the staging buffer busy until the ZLP also completes. */
        if (usbd_ep_start_write(busid, CDC_IN_EP, NULL, 0) != 0) {
            ep_tx_busy_flag = 0;
        }
    } else {
        ep_tx_busy_flag = 0;
    }
}

static struct usbd_endpoint cdc_out_ep = {
    .ep_addr = CDC_OUT_EP,
    .ep_cb = usbd_cdc_acm_bulk_out
};

static struct usbd_endpoint cdc_in_ep = {
    .ep_addr = CDC_IN_EP,
    .ep_cb = usbd_cdc_acm_bulk_in
};

static struct usbd_interface intf0;
static struct usbd_interface intf1;

void usbd_cdc_acm_set_dtr(uint8_t busid, uint8_t intf, bool dtr)
{
    (void)busid;
    (void)intf;
    dtr_enable = dtr ? 1 : 0;
    if (!dtr) {
        cdc_console_reset();
        rx_processed = rx_len; /* Discard any incomplete command on close. */
        banner_sent = 0;
    }
}

void usbd_cdc_acm_set_rts(uint8_t busid, uint8_t intf, bool rts)
{
    (void)busid;
    (void)intf;
    (void)rts;
}

static int cdc_acm_init(uint8_t busid, uintptr_t reg_base)
{
    usbd_desc_register(busid, &cdc_descriptor);

    usbd_add_interface(busid, usbd_cdc_acm_init_intf(busid, &intf0));
    usbd_add_interface(busid, usbd_cdc_acm_init_intf(busid, &intf1));
    usbd_add_endpoint(busid, &cdc_out_ep);
    usbd_add_endpoint(busid, &cdc_in_ep);

    cdc_session_reset();
    return usbd_initialize(busid, reg_base, usbd_event_handler);
}

/* Call only while USB callbacks are excluded by the poll critical section. */
static void cdc_tx_poll(void)
{
    if (!usb_configured || !dtr_enable || ep_tx_busy_flag) {
        return;
    }
    uint32_t n = (uint32_t)cdc_console_peek(write_buffer, sizeof(write_buffer));
    if (n) {
        ep_tx_busy_flag = 1;
        if (usbd_ep_start_write(0, CDC_IN_EP, write_buffer, n) == 0) {
            cdc_console_consume(n);
        } else {
            ep_tx_busy_flag = 0; /* Leave queued data for retry. */
        }
    }
}

static void cdc_poll(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    /* Only one 64-byte packet is parsed per poll. No waits or RTT logging here.
     * This also serializes reset/DTR callbacks with queue and buffer ownership. */
    if (usb_configured && !usb_device_is_configured(0)) {
        cdc_session_reset(); /* SET_CONFIGURATION(0) does not emit a callback. */
    }
    if (usb_configured) {
        cdc_tx_poll();
        if (dtr_enable && !banner_sent &&
            cdc_console_write((const uint8_t *)banner, sizeof(banner) - 1u)) {
            banner_sent = 1;
        }
        if (rx_pending) {
            if (dtr_enable) {
                rx_processed += (uint32_t)cdc_console_feed(
                    read_buffer + rx_processed, rx_len - rx_processed);
            } else {
                rx_processed = rx_len;
            }
            if (rx_processed == rx_len) {
                rx_pending = 0;
            }
        }
        if (!rx_pending && !rx_armed) {
            if (usbd_ep_start_read(0, CDC_OUT_EP, read_buffer, sizeof(read_buffer)) == 0) {
                rx_armed = 1;
            }
        }
        cdc_tx_poll();
    }
    __set_PRIMASK(primask);
}

/* ======================= RTT 日志 ======================= */
int trace_log_printf(const char *fmt, ...)
{
    char buf[192];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n > 0) {
        uint32_t len = ((uint32_t)n < sizeof(buf)) ? (uint32_t)n : (uint32_t)(sizeof(buf) - 1u);
        SEGGER_RTT_Write(0, buf, len);
    }
    return n;
}

/* ======================= CDC 命令行 ======================= */
size_t cdc_console_info(char *out, size_t capacity)
{
    int n = snprintf(out, capacity,
        "chip=STM32H7B0 sysclk=%luHz traceclk=%luHz clk_err=%lu loops=%lu marks=%lu rx=%lu tx=%lu\r\n",
        (unsigned long)g_sysclk_hz, (unsigned long)g_trace.traceclk_hz,
        (unsigned long)g_clock_err, (unsigned long)g_trace.loops,
        (unsigned long)g_trace.marks, (unsigned long)rx_total, (unsigned long)tx_total);
    return n > 0 ? (size_t)n : 0u;
}

void cdc_console_mark(void)
{
    trace_hook_mark();
}

/* ======================= 时钟 ======================= */
/* HAL_RCCEx_GetPeriphCLKFreq() 不支持 RCC_PERIPHCLK_USB（对 USB 恒返回 0），
 * 所以这里直接按寄存器核对：USB 源必须选 PLL3，且 PLL3Q = PLL3 输入 * N / Q 等于 48MHz。 */
static bool usb_clock_is_48mhz(void)
{
    uint32_t cdcc = RCC->CDCCIP2R;
    uint32_t div = RCC->PLL3DIVR;
    uint32_t cksel = RCC->PLLCKSELR;

    if (((cdcc & RCC_CDCCIP2R_USBSEL) >> RCC_CDCCIP2R_USBSEL_Pos) != 2u) {
        return false;
    }
    uint32_t m3 = (cksel & RCC_PLLCKSELR_DIVM3) >> RCC_PLLCKSELR_DIVM3_Pos;
    uint32_t n3 = ((div & RCC_PLL3DIVR_N3) >> RCC_PLL3DIVR_N3_Pos) + 1u;
    uint32_t q3 = ((div & RCC_PLL3DIVR_Q3) >> RCC_PLL3DIVR_Q3_Pos) + 1u;
    if (m3 == 0u || q3 == 0u) {
        return false;
    }
    /* 25MHz 参考：PLL3 输入 = HSE / M3；输出 = 输入 * N3 / Q3 */
    uint32_t pll3q = (HSE_VALUE / m3) * n3 / q3;
    return pll3q == 48000000u;
}

static bool system_clock_init(void)
{
    RCC_OscInitTypeDef osc = { 0 };
    RCC_ClkInitTypeDef clk = { 0 };
    RCC_PeriphCLKInitTypeDef periph = { 0 };

    /* H7 必须先设电压档，否则高频下 PLL/Flash 行为不确定 */
    if (HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY) != HAL_OK) {
        g_clock_err |= 16u;
        return false;
    }
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);
    uint32_t tickstart = HAL_GetTick();
    while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {
        if (HAL_GetTick() - tickstart > 1000u) {
            g_clock_err |= 16u;
            return false;
        }
    }

    /* PLL1: 25MHz /5 *112 = 560MHz VCO -> 280MHz */
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM = 5;
    osc.PLL.PLLN = 112;
    osc.PLL.PLLP = H7_CDC_PLL_P;
    osc.PLL.PLLQ = 3;
    osc.PLL.PLLR = H7_CDC_PLL_R;
    osc.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
    osc.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
    osc.PLL.PLLFRACN = 0;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        g_clock_err |= 1u;
        return false;
    }

    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 |
                    RCC_CLOCKTYPE_D1PCLK1 | RCC_CLOCKTYPE_D3PCLK1;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.SYSCLKDivider = RCC_SYSCLK_DIV1;
    clk.AHBCLKDivider = RCC_HCLK_DIV2;
    clk.APB3CLKDivider = RCC_APB3_DIV2;
    clk.APB1CLKDivider = RCC_APB1_DIV2;
    clk.APB2CLKDivider = RCC_APB2_DIV2;
    clk.APB4CLKDivider = RCC_APB4_DIV4;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_4) != HAL_OK) {
        g_clock_err |= 2u;
        return false;
    }

    /* 25MHz / 5 * 48 / 5 = 48MHz：由外部晶振派生，不依赖 HSI48/CRS。 */
    periph.PeriphClockSelection = RCC_PERIPHCLK_USB;
    periph.UsbClockSelection = RCC_USBCLKSOURCE_PLL3;
    periph.PLL3.PLL3M = 5;
    periph.PLL3.PLL3N = 48;
    periph.PLL3.PLL3P = 2;
    periph.PLL3.PLL3Q = 5;
    periph.PLL3.PLL3R = 2;
    periph.PLL3.PLL3RGE = RCC_PLL3VCIRANGE_2;
    periph.PLL3.PLL3VCOSEL = RCC_PLL3VCOWIDE;
    periph.PLL3.PLL3FRACN = 0;
    if (HAL_RCCEx_PeriphCLKConfig(&periph) != HAL_OK) {
        g_clock_err |= 4u;
        return false;
    }

    g_sysclk_hz = HAL_RCC_GetSysClockFreq();
    if (g_sysclk_hz != 560000000u / H7_CDC_PLL_P || !usb_clock_is_48mhz()) {
        g_clock_err |= 32u;
        return false;
    }

    HAL_PWREx_EnableUSBVoltageDetector();
#if H7_CDC_USB_INTERNAL_REGULATOR
    if (HAL_PWREx_EnableUSBReg() != HAL_OK) {
        g_clock_err |= 8u;
        return false;
    }
#else
    /* 外部 3.3V 接法无需内部稳压器。就绪检测在 USB 连接时由硬件处理。 */
    CLEAR_BIT(PWR->CR3, PWR_CR3_USBREGEN);
#endif
    return true;
}

static void boot_fail(uint32_t error)
{
    g_trace.error = error;
    g_trace.ready = 0;
    trace_log_printf("boot error=0x%08lx clock_err=0x%08lx\r\n",
                     (unsigned long)error, (unsigned long)g_clock_err);
    for (;;) {
        __NOP();
    }
}

/* ======================= main ======================= */
int main(void)
{
    SCB->VTOR = FLASH_BASE;
    __DSB();
    __ISB();
    probe_stage(1);

    HAL_Init();
    SEGGER_RTT_Init();
    trace_hook_init();
    probe_stage(2);

    if (!system_clock_init()) {
        boot_fail(g_clock_err);
    }
    probe_stage(3);

    SystemCoreClockUpdate();
    trace_log_printf("\r\n=== STM32H7B0 CDC ACM (CherryUSB %d.%d.%d) ===\r\n", 1, 6, 1);
    trace_log_printf("SYSCLK=%lu Hz, HCLK=%lu Hz, clock_err=%lu\r\n",
                     (unsigned long)HAL_RCC_GetSysClockFreq(),
                     (unsigned long)HAL_RCC_GetHCLKFreq(),
                     (unsigned long)g_clock_err);
    probe_stage(4);

    /* D-Cache 关闭才能保证 AXI 中的 USB 缓冲与 SWD 视图一致。
     * .noncacheable 只是段名；将来开 D-Cache 时还必须配置 MPU。 */
    SCB_EnableICache();
    probe_stage(5);

    probe_stage(6);

    if (cdc_acm_init(0, USB_OTG_HS_PERIPH_BASE) != 0) {
        boot_fail(0x100u);
    }
    trace_log_printf("usb initialized (USB_OTG_HS_PERIPH_BASE=0x%08lx)\r\n",
                     (unsigned long)USB_OTG_HS_PERIPH_BASE);
    probe_stage(7);

    trace_hook_ready();
    probe_stage(8);

    /* 和 spi_trace_ready 一样：USB/RCC 初始化完成后的稳定业务入口。 */
    trace_hook_entry();
    for (;;) {
        g_trace.loops++;
        cdc_poll();

        if ((g_trace.loops & 0xFFFFFu) == 0u) {
            trace_hook_mark();
        }
    }
}

/* ======================= 中断 ======================= */
/* OTG_HS_IRQHandler 由 usb_glue_st_h7b0.c 提供。 */

void SysTick_Handler(void)
{
    HAL_IncTick();
}

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
