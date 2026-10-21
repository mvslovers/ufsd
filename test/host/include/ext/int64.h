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
/* Called by src/ufsd#fil.c; the test that builds it supplies them. */
extern void __64_from_u32(__64 *r, unsigned v);
extern void __64_mul_u32(__64 *a, unsigned v, __64 *r);
extern void __64_add_u32(__64 *a, unsigned v, __64 *r);
#endif
