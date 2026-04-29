#include "syscall.h"
#include <stdio.h>

int __sys_hello(struct krnl_t *krnl, uint32_t pid, struct sc_regs *regs)
{
    // regs->a1, regs->a2, regs->a3 contain the arguments passed from user-space
    printf("Hello from Kernel! Process %d triggered syscall 18.\n", pid);
    
    // You can also return values back to user-space via a register if needed
    // regs->a3 = 100;
    
    return 0; // 0 usually indicates success
}
