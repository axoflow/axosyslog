    .text
    .global      _perf_trampoline_func_start
    .align 16
_perf_trampoline_func_start:
#ifdef __x86_64__
    endbr64
    sub    $8, %rsp
    mov    _perf_trampoline_target_address(%rip), %r11
    call    *%r11
    add    $8, %rsp
    ret
    .align 8
_perf_trampoline_target_address:
    .quad 0x0
#endif // __x86_64__

    .global      _perf_trampoline_func_end
_perf_trampoline_func_end:
    .section .note.GNU-stack,"",@progbits
