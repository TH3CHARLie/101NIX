/*
  stddef.h
  standard, global definitions
 */
#ifndef _101NIX_STDDEF_H_
#define _101NIX_STDDEF_H_

#define NULL 0x00

#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)

#define container_of(ptr, type, member) ({          \
    const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
    (type *)( (char *)__mptr - offsetof(type,member) );})

#endif