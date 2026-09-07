/* VOIDRUNNER tiny Linux entry.
   Calls C main(argc, argv, envp), then exits via raw syscall. */
.global _start
.extern main
.section .text
_start:
    xor %rbp,%rbp
    mov (%rsp),%rdi                  /* argc */
    lea 8(%rsp),%rsi                 /* argv */
    lea 16(%rsp,%rdi,8),%rdx         /* envp */
    andq $-16,%rsp
    call main
    mov %eax,%edi
    mov $60,%eax                     /* SYS_exit */
    syscall

/* mark stack non-executable so hardened linkers stop warning */
.section .note.GNU-stack,"",@progbits
