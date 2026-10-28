/* CI-only GNU/newlib validation image. MCUXpresso/Redlib remains unchanged. */
ENTRY(ResetISR)
MEMORY {
  FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 256K
  RAM (rwx) : ORIGIN = 0x1fffe000, LENGTH = 32K
}
SECTIONS {
  .text : {
    KEEP(*(.isr_vector))
    . = ALIGN(4);
    __data_section_table = .;
    LONG(LOADADDR(.data)); LONG(ADDR(.data)); LONG(SIZEOF(.data));
    __data_section_table_end = .;
    __bss_section_table = .;
    LONG(ADDR(.bss)); LONG(SIZEOF(.bss));
    __bss_section_table_end = .;
    *(.after_vectors*)
    . = 0x400;
    KEEP(*(.FlashConfig))
    *(.text*) *(.rodata*) *(.constdata*)
    . = ALIGN(4);
    KEEP(*(.init)) KEEP(*(.fini))
  } > FLASH
  .ARM.extab : { *(.ARM.extab*) } > FLASH
  .ARM.exidx : {
    __exidx_start = .; *(.ARM.exidx*) __exidx_end = .;
  } > FLASH
  .mtb (NOLOAD) : { KEEP(*(.mtb*)) } > RAM
  .data : ALIGN(4) {
    _data = .; *(.ramfunc*) *(CodeQuickAccess) *(DataQuickAccess)
    *(RamFunction) *(.data*) . = ALIGN(4); _edata = .;
  } > RAM AT > FLASH
  .bss (NOLOAD) : ALIGN(4) {
    _bss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .;
  } > RAM
  .noinit (NOLOAD) : ALIGN(4) { *(.noinit*) . = ALIGN(4); } > RAM
  .heap (NOLOAD) : ALIGN(8) {
    end = .; _end = .; _pvHeapStart = .; . += 0x800; _pvHeapLimit = .;
  } > RAM
  .stack ORIGIN(RAM) + LENGTH(RAM) - 0x800 (NOLOAD) : {
    _vStackBase = .; . += 0x800; _vStackTop = .;
  } > RAM
  ASSERT(_pvHeapLimit <= _vStackBase, "MCXC444 RAM/heap/stack overlap")
  ASSERT(SIZEOF(.text) > 0x410, "MCXC444 image missing flash configuration/code")
}
