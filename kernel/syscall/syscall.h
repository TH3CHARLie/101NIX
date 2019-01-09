#ifndef _SYSCALL_H
#define _SYSCALL_H

#include <zjunix/pc.h>

void syscall4(unsigned int status, unsigned int cause, context* pt_context);
void syscall50(unsigned int status, unsigned int cause, context* pt_context);
void syscall51(unsigned int status, unsigned int cause, context* pt_context);
void syscall60(unsigned int status, unsigned int cause, context* pt_context);
void syscall61(unsigned int status, unsigned int cause, context* pt_context);

#endif  // ! _SYSCALL_H