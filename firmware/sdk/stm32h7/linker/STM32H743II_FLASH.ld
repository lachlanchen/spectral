ENTRY(Reset_Handler)

_estack = ORIGIN(RAM_D1) + LENGTH(RAM_D1);
_Min_Heap_Size = 0x400;
_Min_Stack_Size = 0x4000;

MEMORY
{
  FLASH   (rx)  : ORIGIN = 0x08000000, LENGTH = 2048K
  DTCMRAM (xrw) : ORIGIN = 0x20000000, LENGTH = 128K
  RAM_D1  (xrw) : ORIGIN = 0x24000000, LENGTH = 512K
  RAM_D2  (xrw) : ORIGIN = 0x30000000, LENGTH = 256K
  RAM_D3  (xrw) : ORIGIN = 0x38000000, LENGTH = 64K
}

PHDRS
{
  text PT_LOAD FLAGS(5);
  data PT_LOAD FLAGS(6);
  ram  PT_LOAD FLAGS(6);
}

SECTIONS
{
  .isr_vector : { KEEP(*(.isr_vector)) } > FLASH :text
  .text :
  {
    *(.text*) *(.rodata*)
    KEEP(*(.init)) KEEP(*(.fini))
    . = ALIGN(4);
    _etext = .;
  } > FLASH :text
  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FLASH :text
  .ARM.exidx :
  {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } > FLASH :text
  .preinit_array : { PROVIDE_HIDDEN(__preinit_array_start = .); KEEP(*(.preinit_array*)) PROVIDE_HIDDEN(__preinit_array_end = .); } > FLASH :text
  .init_array : { PROVIDE_HIDDEN(__init_array_start = .); KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array*)) PROVIDE_HIDDEN(__init_array_end = .); } > FLASH :text
  .fini_array : { PROVIDE_HIDDEN(__fini_array_start = .); KEEP(*(SORT(.fini_array.*))) KEEP(*(.fini_array*)) PROVIDE_HIDDEN(__fini_array_end = .); } > FLASH :text

  _sidata = LOADADDR(.data);
  .data :
  {
    . = ALIGN(4); _sdata = .;
    *(.data*)
    . = ALIGN(4); _edata = .;
  } > RAM_D1 AT > FLASH :data

  .dma_buffer (NOLOAD) :
  {
    . = ALIGN(32); __dma_buffer_start__ = .;
    *(.dma_buffer*)
    . = ALIGN(32); __dma_buffer_end__ = .;
  } > RAM_D2 :ram

  .bss (NOLOAD) :
  {
    . = ALIGN(8); _sbss = .; __bss_start__ = .;
    *(.bss*) *(COMMON)
    . = ALIGN(8); _ebss = .; __bss_end__ = .;
  } > RAM_D1 :ram

  ._user_heap_stack (NOLOAD) :
  {
    . = ALIGN(8); PROVIDE(end = .); PROVIDE(_end = .);
    . += _Min_Heap_Size;
    . += _Min_Stack_Size;
    . = ALIGN(8);
  } > RAM_D1 :ram

  /DISCARD/ : { *(.note.GNU-stack) *(.gnu_debuglink) }
  .ARM.attributes 0 : { *(.ARM.attributes) } :NONE
}
