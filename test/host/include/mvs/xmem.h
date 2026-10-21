/* Host stand-in for libc370's <mvs/xmem.h>: prototypes only. */
#ifndef HOSTSHIM_MVS_XMEM_H
#define HOSTSHIM_MVS_XMEM_H
void __xmpost(void *ascb, void *ecb, unsigned postcode);
#endif
