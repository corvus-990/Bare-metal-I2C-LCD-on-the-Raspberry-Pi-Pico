ENTRY(bootStage2);

MEMORY
{
    flash(rx) : ORIGIN = 0x10000000, LENGTH = 2048k
    sram(rwx) : ORIGIN = 0x20000000, LENGTH = 256k
}

SECTIONS
{
    .boot2 :
    {
        _sboot2 = .;
        KEEP(*(.boot2*))
        _eboot2 = .;
        . = . + (252 - (_eboot2 - _sboot2));
        KEEP(*(.crc*))
    } > flash

    .text :
    {
        KEEP(*(.text.main))
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > flash

    .data : ALIGN(4)
    {
        _data_start = .;
        *(.data*)
        _data_end = .;
    } > sram AT > flash
    _data_load = LOADADDR(.data);

    .bss : ALIGN(4)
    {
        _bss_start = .;
        *(.bss*)
        *(COMMON)
        _bss_end = .;
    } > sram

    /DISCARD/ :
    {
        *(.comment)
        *(.ARM.attributes)
    }
}
