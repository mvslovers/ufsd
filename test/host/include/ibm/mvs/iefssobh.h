/* Host stand-in for the SSOB layout in libc370's <ibm/mvs/iefssobh.h>.
** Field names and order follow the IBM header; pointers are native width. */
#ifndef HOSTSHIM_IEFSSOBH_H
#define HOSTSHIM_IEFSSOBH_H
#define SSRTOK      0
#define SSRTNSUP    4
#define SSRTNTUP    8
#define SSRTNOSS    12
#define SSRTDIST    16
#define SSRTLERR    20
typedef struct ssob SSOB;
struct ssob {
    unsigned char   SSOBID[4];
#define SSOBID_EYE  "SSOB"
    unsigned short  SSOBLEN;
    unsigned short  SSOBFUNC;
    void           *SSOBSSIB;
    int             SSOBRETN;
    void           *SSOBINDV;
};
#endif
