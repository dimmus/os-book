// Vector 32 (IRQ0 / PIT): same frame as #BP — no error code on stack.
// See OS-6-irq-pic-study.md.

.text
.globl isr_irq32
.balign 16
isr_irq32:
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
    call timer_irq_dispatch
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
    movw $0x10, 32(%rsp)
    iretq
