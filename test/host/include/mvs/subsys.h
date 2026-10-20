/* Host stand-in for libc370's <mvs/subsys.h>: opaque SSCT/SSVT and the one
** call libufs makes.  The test supplies iefssreq(); that is the seam. */
#ifndef HOSTSHIM_MVS_SUBSYS_H
#define HOSTSHIM_MVS_SUBSYS_H
#include <ibm/mvs/iefssobh.h>
typedef struct ssct SSCT;
typedef struct ssvt SSVT;
extern int iefssreq(SSOB *ssob);
#endif
