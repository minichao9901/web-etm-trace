/*
 * STM32F103 + CherryUSB CDC ACM 例程（etm-trace 目标固件）
 * ============================================================================
 * 目录定位：
 *   E:\web-etm-trace\firmware\stm32f103ze_cdc_trace
 * 两个目的：
 *   1) 把 CherryUSB 1.6.1 的 demo/cdc_acm_template.c 跑到 STM32F103 上（FS 设备，
 *      PA11/PA12，USB 时钟 48MHz），做成一个可以立刻上手测的 CDC 环回 + 命令行；
 *   2) 作为 **etm-trace 的被测目标**：提供稳定入口 trace_hook_entry()、就绪标志、
 *      DWT 周期打点，具体 trace 由外部 SWD 工具配置（见 src/trace_hooks.h 的说明）。
 *
 * CDC 命令（115200 8N1，任意串口工具 / Python 脚本）：
 *   PING            -> PONG
 *   INFO            -> 芯片/时钟/版本/计数
 *   MARK            -> 打一次 trace 点并回 OK MARK
 *   ECHO <text>     -> 原样回显
 *   其它            -> ERR UNKNOWN
 * 另外：**任何收到的字节都会原样回显**（ECHO_ON 默认开），方便用回环脚本测吞吐。
 *
 * 构建：pwsh -NoProfile -File .\build.ps1 -Chip cb      (默认，对应在线板)
 *       pwsh -NoProfile -File .\build.ps1 -Chip ze      (目标板，512K/64K)
 */
#include "stm32f1xx_hal.h"
#include "usbd_core.h"
#include "usbd_cdc_acm.h"
#include "trace_hooks.h"
#include "SEGGER_RTT.h"

#include <string.h>
#include <stdio.h>

/* system_clock.c */
extern uint32_t g_sysclk_hz;
extern uint32_t g_clock_err;
void system_clock_init(void);

/* 主动断开-重连 USB（等价拔插一次线），见 main 里的说明 */
void usb_reconnect(uint32_t hold_ms);

/* boot_probe.c：启动阶段探针（PA1 脉冲 + g_boot_stage） */
extern volatile uint32_t g_boot_stage;
void probe_stage(uint32_t stage);
void probe_gpio_init(void);

/* ======================= CherryUSB CDC ACM 描述符 ======================= */
/* 端点号与 cdc_acm_template.c 保持一致：INT=0x83 / OUT=0x02 / IN=0x81 */
#define CDC_IN_EP  0x81
#define CDC_OUT_EP 0x02
#define CDC_INT_EP 0x83

#define USBD_VID           0xF055
#define USBD_PID           0x1031
#define USBD_MAX_POWER     100
#define USBD_LANGID_STRING 1033

#define USB_CONFIG_SIZE (9 + CDC_ACM_DESCRIPTOR_LEN)

/* F103 只有全速（FS）USB，MPS 固定 64 */
#define CDC_MAX_MPS 64

/* 缓冲大小：模板里是 2048（跑吞吐测试用），F103CB 只有 20KB RAM 且 USB 无 DMA，
 * 这里缩到 512，仍然远大于 64 字节的包长，逻辑完全一致。 */
#define CDC_BUF_SIZE 512

static const uint8_t device_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0xEF, 0x02, 0x01, USBD_VID, USBD_PID, 0x0100, 0x01)
};

static const uint8_t config_descriptor[] = {
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x02, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    CDC_ACM_DESCRIPTOR_INIT(0x00, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, CDC_MAX_MPS, 0x02)
};

static const uint8_t device_quality_descriptor[] = {
    /* device qualifier descriptor（FS 设备也要给，Windows 会问） */
    0x0a, USB_DESCRIPTOR_TYPE_DEVICE_QUALIFIER,
    0x00, 0x02, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00,
};

static const char *string_descriptors[] = {
    (const char[]){ 0x09, 0x04 },  /* Langid = 1033 (US English) */
    "web-etm-trace",               /* Manufacturer */
    "STM32F103 CDC ACM (CherryUSB)", /* Product */
    "F103CDC0001",                 /* Serial Number */
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

static const uint8_t *device_quality_descriptor_callback(uint8_t speed)
{
    (void)speed;
    return device_quality_descriptor;
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
    .device_quality_descriptor_callback = device_quality_descriptor_callback,
    .string_descriptor_callback = string_descriptor_callback
};

/* ======================= 端点缓冲 ======================= */
/* 2 字节对齐即可：F1 的 PMA 访问由 CherryUSB 的 fsdev 驱动按 16 位搬，不需要 DMA 对齐 */
static uint8_t read_buffer[CDC_BUF_SIZE];
static uint8_t write_buffer[CDC_BUF_SIZE];
static volatile uint32_t rx_len;       /* 最近一次 OUT 收到的长度 */
static volatile uint32_t rx_total;     /* 累计收到 */
static volatile uint32_t tx_total;     /* 累计发出 */
static volatile uint8_t ep_tx_busy_flag;

static volatile uint8_t dtr_enable;
static volatile uint8_t echo_enable = 1; /* 默认打开：收到什么回什么 */

static const char banner[] =
    "\r\n=== STM32F103 CherryUSB CDC ACM (etm-trace target) ===\r\n"
    "commands: PING | INFO | MARK | ECHO <text>\r\n";

/* ======================= 事件回调 ======================= */
static void usbd_event_handler(uint8_t busid, uint8_t event)
{
    switch (event) {
        case USBD_EVENT_RESET:
            USB_LOG_RAW("[usb] reset\r\n");
            break;
        case USBD_EVENT_CONNECTED:
            USB_LOG_RAW("[usb] connected\r\n");
            break;
        case USBD_EVENT_DISCONNECTED:
            USB_LOG_RAW("[usb] disconnected\r\n");
            break;
        case USBD_EVENT_RESUME:
        case USBD_EVENT_SUSPEND:
            break;
        case USBD_EVENT_CONFIGURED:
            USB_LOG_RAW("[usb] configured\r\n");
            ep_tx_busy_flag = 0;
            /* 模板原样：配好之后先挂一次 OUT 读请求 */
            usbd_ep_start_read(busid, CDC_OUT_EP, read_buffer, CDC_BUF_SIZE);
            break;
        case USBD_EVENT_SET_REMOTE_WAKEUP:
        case USBD_EVENT_CLR_REMOTE_WAKEUP:
            break;
        default:
            break;
    }
}

/* OUT：主机发来的数据 */
void usbd_cdc_acm_bulk_out(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)ep;
    rx_len = nbytes;
    rx_total += nbytes;
    g_trace.usb_rx = rx_total;

    /* 立刻挂下一次读，避免主机侧等太久 */
    usbd_ep_start_read(busid, CDC_OUT_EP, read_buffer, CDC_BUF_SIZE);
}

/* IN：上一次发送完成 */
void usbd_cdc_acm_bulk_in(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    tx_total += nbytes;
    g_trace.usb_tx = tx_total;

    if ((nbytes % usbd_get_ep_mps(busid, ep)) == 0 && nbytes) {
        /* 整包结束要补 ZLP（模板原样） */
        usbd_ep_start_write(busid, CDC_IN_EP, NULL, 0);
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

/* CDC 类请求回调（模板里是 weak，这里给出真实实现） */
void usbd_cdc_acm_set_dtr(uint8_t busid, uint8_t intf, bool dtr)
{
    (void)busid;
    (void)intf;
    dtr_enable = dtr ? 1 : 0;
}

void usbd_cdc_acm_set_rts(uint8_t busid, uint8_t intf, bool rts)
{
    (void)busid;
    (void)intf;
    (void)rts;
}

void cdc_acm_init(uint8_t busid, uintptr_t reg_base)
{
    memset(read_buffer, 0, sizeof(read_buffer));
    memset(write_buffer, 0, sizeof(write_buffer));

    usbd_desc_register(busid, &cdc_descriptor);

    usbd_add_interface(busid, usbd_cdc_acm_init_intf(busid, &intf0));
    usbd_add_interface(busid, usbd_cdc_acm_init_intf(busid, &intf1));
    usbd_add_endpoint(busid, &cdc_out_ep);
    usbd_add_endpoint(busid, &cdc_in_ep);

    usbd_initialize(busid, reg_base, usbd_event_handler);
    /* 到此为止，与参照工程 cherry_drag_n_drop_bootloader_fat16 完全一致：
     *   usbd_initialize() -> usb_dc_init() -> usb_dc_low_level_init()（开 USB 时钟 + 两条中断）
     * F1 的 D+ 上拉是常态接通的（没有 BCDR/DPPU 位），所以这里不需要做任何"连接"动作。 */
}

/* ======================= 发送（阻塞到写完，裸机够用） ======================= */
static void cdc_write(const uint8_t *data, uint32_t len)
{
    if (!dtr_enable || len == 0 || len > CDC_BUF_SIZE) {
        return;
    }
    if (ep_tx_busy_flag) {
        return; /* 上一次还没发完，丢这一条（不阻塞主循环） */
    }
    memcpy(write_buffer, data, len);
    ep_tx_busy_flag = 1;
    if (usbd_ep_start_write(0, CDC_IN_EP, write_buffer, len) != 0) {
        ep_tx_busy_flag = 0;
    }
}

static void cdc_puts(const char *s)
{
    cdc_write((const uint8_t *)s, (uint32_t)strlen(s));
}

/* ======================= RTT 日志（给 CherryUSB 的 CONFIG_USB_PRINTF） ======================= */
int trace_log_printf(const char *fmt, ...)
{
    char buf[160];
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
static void handle_command(const uint8_t *buf, uint32_t len)
{
    char line[128];
    uint32_t n = (len < sizeof(line) - 1u) ? len : (sizeof(line) - 1u);

    memcpy(line, buf, n);
    line[n] = '\0';

    /* 去掉行尾 CR/LF */
    while (n > 0u && (line[n - 1u] == '\r' || line[n - 1u] == '\n')) {
        line[--n] = '\0';
    }

    if (strcmp(line, "PING") == 0) {
        cdc_puts("PONG\r\n");
    } else if (strcmp(line, "INFO") == 0) {
        char out[200];
        snprintf(out, sizeof(out),
                 "chip=%s sysclk=%luHz clk_err=%lu loops=%lu marks=%lu rx=%lu tx=%lu "
                 "irq=%lu ctr=%lu rst=%lu err=%lu susp=%lu\r\n",
                 "STM32F103", (unsigned long)g_sysclk_hz, (unsigned long)g_clock_err,
                 (unsigned long)g_trace.loops, (unsigned long)g_trace.marks,
                 (unsigned long)rx_total, (unsigned long)tx_total,
                 (unsigned long)g_trace.usb_irq, (unsigned long)g_trace.usb_ctr,
                 (unsigned long)g_trace.usb_reset, (unsigned long)g_trace.usb_err,
                 (unsigned long)g_trace.usb_susp);
        cdc_puts(out);
    } else if (strcmp(line, "MARK") == 0) {
        trace_hook_mark();
        cdc_puts("OK MARK\r\n");
    } else if (strcmp(line, "ECHO on") == 0) {
        echo_enable = 1;
        cdc_puts("OK ECHO on\r\n");
    } else if (strcmp(line, "ECHO off") == 0) {
        echo_enable = 0;
        cdc_puts("OK ECHO off\r\n");
    } else if (strncmp(line, "ECHO ", 5) == 0) {
        cdc_write((const uint8_t *)(line + 5), n - 5u);
        cdc_puts("\r\n");
    } else if (n == 0u) {
        /* 空行忽略 */
    } else {
        cdc_puts("ERR UNKNOWN\r\n");
    }
}

/* ======================= main ======================= */
int main(void)
{
    /* 启动阶段探针：每过一步 PA1 打一个 ~20us 脉冲，g_boot_stage 记录步号。
     * 万一以后又在某处卡死，读 g_boot_stage 就知道停在哪（见 src/boot_probe.c）。 */
    probe_stage(1);

    /* 1) 时钟：72MHz SYSCLK / 48MHz USB
     *    ⚠️ 顺序很关键：**先配时钟，再 HAL_Init()**。
     *    反过来的话，HAL_Init() 会用复位默认的 HSI(8MHz) 去算 SysTick 重载值并立刻开中断，
     *    随后 system_clock_init() 把主频抬到 72MHz —— SysTick 周期就变成 9 倍误差，
     *    而且这段窗口里任何 HAL 断言/等待都可能被 1ms 中断反复打断。 */
    system_clock_init();
    probe_stage(2);

    /* 2) HAL 初始化（只做 NVIC 优先级分组 + 1ms 时基，时钟已经稳定） */
    HAL_Init();
    SystemCoreClockUpdate();
    probe_stage(3);

    /* 3) RTT 日志通道（不需要 SWO 引脚） */
    SEGGER_RTT_Init();
    trace_log_printf("\r\n=== STM32F103 CDC ACM (CherryUSB %d.%d.%d) ===\r\n",
                     1, 6, 1);
    trace_log_printf("SYSCLK=%lu Hz, clock_err=%lu\r\n",
                     (unsigned long)g_sysclk_hz, (unsigned long)g_clock_err);
    probe_stage(4);

    /* 4) trace 钩子：DWT 周期计数器 + 打点 GPIO */
    trace_hook_init();
    probe_stage(5);

    /* 5) USB 设备初始化（内部会调 usb_dc_low_level_init） */
    trace_hook_entry();
    cdc_acm_init(0, (uintptr_t)USB_BASE);
    trace_log_printf("usb initialized (USB_BASE=0x%08lx)\r\n", (unsigned long)USB_BASE);
    probe_stage(6);

    trace_hook_ready();
    probe_stage(7);

    /* 6) 主循环：等 CDC 数据 -> 命令行 + 可选的原始回显 -> 周期性打点 */
    for (;;) {
        g_trace.loops++;

        if (rx_len) {
            uint32_t n = rx_len;
            uint8_t snapshot[CDC_BUF_SIZE];

            /* 在中断再次改写 read_buffer 之前先快照 */
            memcpy(snapshot, read_buffer, n);
            rx_len = 0;

            /* 先做最原始的"收到什么回什么"（可用 ECHO off 关掉），
             * 再交给命令行解析；两步各自只有一次 cdc_write，不会互相挤掉。 */
            if (echo_enable) {
                cdc_write(snapshot, n);
            }
            handle_command(snapshot, n);

            trace_log_printf("[cdc] rx %lu bytes\r\n", (unsigned long)n);
        }

        /* 每约 1M 次空转打一个 trace 点（没有计时器也能看出执行流） */
        if ((g_trace.loops & 0xFFFFFu) == 0u) {
            trace_hook_mark();
        }

        /* 首次进入主循环后把 banner 推给主机（DTR 被拉起来才发得出去） */
        static uint8_t banner_sent = 0;
        if (!banner_sent && dtr_enable && !ep_tx_busy_flag) {
            cdc_puts(banner);
            banner_sent = 1;
        }
    }
}
