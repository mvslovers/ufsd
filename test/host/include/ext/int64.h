/* Host stand-in for libc370's <ext/int64.h>: the type only.
** The real __64 arithmetic is software on S/370 and is verified on the
** target; libufs.c never calls it. */
#ifndef HOSTSHIM_EXT_INT64_H
#define HOSTSHIM_EXT_INT64_H
#include <stdint.h>
typedef union {
    uint64_t u64;
    uint32_t u32[2];
} __64;
#endif
