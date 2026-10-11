ENTRY(Reset_Handler)
MEMORY {
  FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 128K
  DTCM (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
  AXI (rwx) : ORIGIN = 0x24000000, LENGTH = 1M
}
_estack = ORIGIN(DTCM) + LENGTH(DTCM);
SECTIONS {
  .isr_vector : { KEEP(*(.isr_vector)) } > FLASH
  .text : { *(.text*) *(.rodata*) KEEP(*(.init)) KEEP(*(.fini)) . = ALIGN(4); } > FLASH
  .ARM.extab : { *(.ARM.extab*) } > FLASH
  .ARM.exidx : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } > FLASH
  .preinit_array : { PROVIDE_HIDDEN(__preinit_array_start = .); KEEP(*(.preinit_array*)) PROVIDE_HIDDEN(__preinit_array_end = .); } > FLASH
  .init_array : { PROVIDE_HIDDEN(__init_array_start = .); KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array)) PROVIDE_HIDDEN(__init_array_end = .); } > FLASH
  .fini_array : { PROVIDE_HIDDEN(__fini_array_start = .); KEEP(*(SORT(.fini_array.*))) KEEP(*(.fini_array)) PROVIDE_HIDDEN(__fini_array_end = .); } > FLASH
  _sidata = LOADADDR(.data);
  .data : { . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > AXI AT> FLASH
  .bss (NOLOAD) : { . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > AXI
  . = ALIGN(8); _end = .; PROVIDE(end = .);
  _heap_limit = ORIGIN(AXI) + LENGTH(AXI);
  /* SEGGER_RTT_Init explicitly zeros this CB; preserve the original address. */
  .rtt_cb 0x20002000 (NOLOAD) : { KEEP(*(.rtt_cb)) } > DTCM
  .trace_diag 0x20003000 (NOLOAD) : { KEEP(*(.trace_diag)) } > DTCM
  ASSERT(_heap_limit - _end >= 4096, "AXI SRAM has no heap margin")
}
