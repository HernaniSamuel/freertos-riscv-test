/* boot.S — bare-metal RV32IM entry point */
.section .text.boot
.global _start

_start:
    la sp, __freertos_irq_stack_top

    la t0, freertos_risc_v_trap_handler
    csrw mtvec, t0

    call main

halt:
    j halt