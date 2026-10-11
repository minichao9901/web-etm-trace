#include "main.h"
#include "spi_trace_support.h"
/* Debugger-visible diagnostics bypass the M7 data cache in DTCM. */
volatile SpiTraceDiagnostics g_spi_trace __attribute__((section(".trace_diag"), aligned(4)));
/* A stable breakpoint address for the host; this firmware never enables ETM. */
__attribute__((noinline)) void spi_trace_ready(void) {
    g_spi_trace.cpu_hz = HAL_RCC_GetSysClockFreq();
    g_spi_trace.hclk_hz = HAL_RCC_GetHCLKFreq();
    g_spi_trace.spi_kernel_hz = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SPI2);
    g_spi_trace.traceclk_hz = 560000000U / SPI_TRACE_PLL_R / 2U;
    g_spi_trace.ready = 1;
}
