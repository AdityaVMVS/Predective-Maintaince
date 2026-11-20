ENTRY(Reset_Handler)
MEMORY {
    FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 2M
    DTCM (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
    SRAM1 (rwx) : ORIGIN = 0x20020000, LENGTH = 368K
}
_estack = ORIGIN(DTCM) + LENGTH(DTCM);
SECTIONS {
    .isr_vector : { KEEP(*(.isr_vector)) } > FLASH
    .text : { *(.text*) *(.rodata*) KEEP(*(.init)) KEEP(*(.fini)) . = ALIGN(4); } > FLASH
    .ARM.extab : { *(.ARM.extab*) } > FLASH
    .ARM.exidx : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } > FLASH
    .init_array : {
        __preinit_array_start = .; KEEP(*(.preinit_array*)) __preinit_array_end = .;
        __init_array_start = .; KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array)) __init_array_end = .;
        __fini_array_start = .; KEEP(*(.fini_array*)) __fini_array_end = .;
    } > FLASH
    .data : { . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > DTCM AT > FLASH
    _sidata = LOADADDR(.data);
    .bss (NOLOAD) : { . = ALIGN(8); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(8); _ebss = .; } > DTCM
    .dma (NOLOAD) : { . = ALIGN(32); *(.dma) . = ALIGN(32); } > SRAM1
    end = _ebss;
}
ASSERT(_estack - _ebss >= 4096, "Reserve room for the interrupt stack")
