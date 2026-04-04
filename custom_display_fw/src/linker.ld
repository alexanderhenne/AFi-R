/* STM32F2xx linker script for AFi-R LCD application firmware */
/* App starts at 0x08020000 (after 128KB bootloader) */

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08020000, LENGTH = 896K
    RAM (rwx)   : ORIGIN = 0x20000000, LENGTH = 128K
}

_estack = ORIGIN(RAM) + LENGTH(RAM) - 1;  /* 0x2001FFFF */

SECTIONS
{
    .isr_vector : {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } >FLASH

    /* Version string at fixed offset 0x200 from flash start */
    .version 0x08020200 : {
        KEEP(*(.version))
        . = ALIGN(4);
    } >FLASH

    .text : {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
        _etext = .;
    } >FLASH

    _sidata = LOADADDR(.data);

    .data : {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } >RAM AT> FLASH

    .bss : {
        . = ALIGN(4);
        _sbss = .;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } >RAM

    /* Discard ARM exception unwinding tables (not used in bare-metal) */
    /DISCARD/ : {
        *(.ARM.exidx*)
        *(.ARM.extab*)
    }

    /* Tail tag at the end of the firmware image */
    .tailtag 0x080FFBF0 : {
        KEEP(*(.tailtag))
    } >FLASH
}
