// Vector 14 (#PF): CPU pushes an error code below RIP/CS/RFLAGS/RSP/SS.
// Stage 5 serial markers; see OS-8-page-fault-study.md.

.text
.globl isr_page_fault
.balign 16
isr_page_fault:
    pushq %rax
    pushq %rcx
    pushq %rdx
    pushq %rbx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    subq $8, %rsp
    /* Error code sits 128 bytes above current RSP (15*8 + align). */
    leaq 128(%rsp), %rdi
    call page_fault_dispatch
    addq $8, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rbx
    popq %rdx
    popq %rcx
    popq %rax
    addq $8, %rsp
    movw $0x10, 32(%rsp)
    iretq
