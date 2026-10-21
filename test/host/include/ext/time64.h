/* Host stand-in for libc370's <ext/time64.h>: the types only. */
#ifndef HOSTSHIM_EXT_TIME64_H
#define HOSTSHIM_EXT_TIME64_H
#include <ext/int64.h>
typedef __64 mtime64_t;
extern void mtime64(mtime64_t *t);
#endif
