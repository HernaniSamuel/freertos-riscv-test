ENTRY(_start)

MEMORY
{
    /* ORIGIN deve bater com o RAM_BASE (0x80000000) do seu emulador  */
    RAM (rwx) : ORIGIN = 0x80000000, LENGTH = 32M
}

SECTIONS
{
    /* ------------------------------------------------------------------ */
    /* Text: code and read-only data                                       */
    /* ------------------------------------------------------------------ */
    .text ORIGIN(RAM) :
    {
        /* Trap vector must be at a known address; mtvec will point here.
         * The boot stub in main.c sets mtvec after startup.             */
        KEEP(*(.text.boot))     /* boot entry point first               */
        *(.text*)
        *(.rodata*)
    } > RAM

    /* ------------------------------------------------------------------ */
    /* Initialized data                                                    */
    /* ------------------------------------------------------------------ */
    .data :
    {
        _data_start = .;
        *(.data*)
        _data_end = .;
    } > RAM

    /* ------------------------------------------------------------------ */
    /* Zero-initialized data                                               */
    /* ------------------------------------------------------------------ */
    .bss :
    {
        _bss_start = .;
        *(.bss*)
        *(COMMON)
        _bss_end = .;
    } > RAM

    /* ------------------------------------------------------------------ */
    /* FreeRTOS heap (grows up from here, stack grows down from RAM top)  */
    /* ------------------------------------------------------------------ */
    _heap_start = _bss_end;
    _heap_end   = ORIGIN(RAM) + LENGTH(RAM);

    /* ------------------------------------------------------------------ */
    /* FreeRTOS IRQ stack top                                             */
    /* ------------------------------------------------------------------ */
    __freertos_irq_stack_top = ORIGIN(RAM) + LENGTH(RAM);
}
