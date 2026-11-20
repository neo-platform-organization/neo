.text
.global neo_x86_64_enter
.type neo_x86_64_enter,@function
neo_x86_64_enter:
    cli
    mov %rdi,%rsp
    and $-16,%rsp
    xor %rbp,%rbp
    call neo_x86_64_run
1:  hlt
    jmp 1b
.global neo_fault_entry
.type neo_fault_entry,@function
neo_fault_entry:
    cli
    lea neo_fault_text(%rip),%rdi
    call neo_x86_64_panic
2:  hlt
    jmp 2b
.section .rodata
neo_fault_text: .asciz "CPU exception in trusted development image"
.section .note.GNU-stack,"",@progbits
