/* Host stand-in for libc370's <mvs/apf.h>: prototypes only. */
#ifndef HOSTSHIM_MVS_APF_H
#define HOSTSHIM_MVS_APF_H
#define PSWKEY0 0x00
int __super(unsigned char pswkey, unsigned char *savekey);
int __prob(unsigned char pswkey, unsigned char *savekey);
#endif
