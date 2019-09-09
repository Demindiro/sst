#ifndef INTERPRETER_SYSCALL_H
#define INTERPRETER_SYSCALL_H

#include <stdint.h>

void vasm_syscall(int64_t regs[32], uint8_t *mem);

#endif
