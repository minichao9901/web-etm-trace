#pragma once
#include <stdint.h>
#ifndef SPI_TRACE_PLL_P
#define SPI_TRACE_PLL_P 2
#endif
#ifndef SPI_TRACE_PLL_R
#define SPI_TRACE_PLL_R 4
#endif
typedef struct {
    uint32_t ready, error, cpu_hz, hclk_hz, spi_kernel_hz, traceclk_hz;
    uint32_t transactions, spi_status;
} SpiTraceDiagnostics;
extern volatile SpiTraceDiagnostics g_spi_trace;
void spi_trace_ready(void);
