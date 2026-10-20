/* Host stand-in for libc370's <ibm/mvs/iefjssib.h>: the SSIB fields libufs sets. */
#ifndef HOSTSHIM_IEFJSSIB_H
#define HOSTSHIM_IEFJSSIB_H
typedef struct ssib SSIB;
struct ssib {
    unsigned char   SSIBID[4];
    unsigned short  SSIBLEN;
    unsigned char   SSIBFLG1;
    unsigned char   SSIBSSID;
    unsigned char   SSIBSSNM[4];
    unsigned char   SSIBJBID[8];
    unsigned char   SSIBDEST[8];
    unsigned        SSIBRSV1;
    unsigned        SSIBSUSE;
};
#endif
