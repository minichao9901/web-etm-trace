/*
 * STM32F103 链接脚本（GCC / arm-none-eabi）
 *
 * 先用 -Chip cb 对应的 stm32f103cb.ld（64K Flash / 20K RAM），
 * 目标板 F103ZE 用 stm32f103ze.ld（512K Flash / 64K RAM）。
 *
 * 段布局说明：
 *   .isr_vector                        -> Flash，启动文件里定义
 *   .text/.rodata/.data/.bss           -> Flash / RAM（标准布局）
 *   .trace_diag 固定到 RAM 末尾 - 64B    -> 外部 SWD 工具按固定地址读诊断变量
 *   .rtt_cb     固定到 RAM 末尾 - 1K     -> RTT 控制块固定地址，便于主机指定查找范围
 */
ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 64K
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
_Min_Heap_Size  = 0x200;
_Min_Stack_Size = 0x400;

SECTIONS
{
  .isr_vector : {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } > FLASH

  .text : {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)
    KEEP(*(.init))
    KEEP(*(.fini))
    . = ALIGN(4);
    _etext = .;
  } > FLASH

  .rodata : {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } > FLASH

  .ARM.extab : {
    . = ALIGN(4);
    *(.ARM.extab* .gnu.linkonce.armextab.*)
    . = ALIGN(4);
  } > FLASH

  .ARM : {
    . = ALIGN(4);
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
    . = ALIGN(4);
  } > FLASH

  .preinit_array : {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__preinit_array_start = .);
    KEEP(*(.preinit_array*))
    PROVIDE_HIDDEN(__preinit_array_end = .);
    . = ALIGN(4);
  } > FLASH

  .init_array : {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    PROVIDE_HIDDEN(__init_array_end = .);
    . = ALIGN(4);
  } > FLASH

  .fini_array : {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array*))
    PROVIDE_HIDDEN(__fini_array_end = .);
    . = ALIGN(4);
  } > FLASH

  _sidata = LOADADDR(.data);

  .data : {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } > RAM AT> FLASH

  . = ALIGN(4);
  .bss : {
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } > RAM

  /* 栈/堆余量检查：低于这个值直接报链接错误 */
  ._user_heap_stack : {
    . = ALIGN(8);
    PROVIDE(end = .);
    PROVIDE(_end = .);
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } > RAM

  /* 诊断变量：固定在高地址，外部工具按 0x2000xxxx 直接读 */
  .trace_diag ORIGIN(RAM) + LENGTH(RAM) - 0x40 (NOLOAD) : {
    . = ALIGN(4);
    KEEP(*(.trace_diag))
    . = ALIGN(4);
  } > RAM

  /* RTT 控制块：固定地址，方便主机 -RTTSearchRanges / pyocd 指定 */
  .rtt_cb ORIGIN(RAM) + LENGTH(RAM) - 0x440 (NOLOAD) : {
    . = ALIGN(4);
    KEEP(*(.rtt_cb))
    . = ALIGN(4);
  } > RAM

  /DISCARD/ : {
    *(.ARM.attributes)
  }
}
