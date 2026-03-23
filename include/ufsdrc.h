/* UFSDRC.H - UFSD Return Codes
**
** Shared between libufs.h (client API) and ufsd.h (server internals).
** Include this header directly when only return codes are needed.
*/

#ifndef UFSDRC_H
#define UFSDRC_H

#define UFSD_RC_OK          0    /* success                          */
#define UFSD_RC_NOREQ       4    /* no free request block available  */
#define UFSD_RC_CORRUPT     8    /* corrupt request block (eye fail) */
#define UFSD_RC_BADFUNC     12   /* invalid function code            */
#define UFSD_RC_BADSESS     16   /* invalid session token            */
#define UFSD_RC_INVALID     20   /* validation failed                */
#define UFSD_RC_NOTIMPL     24   /* not yet implemented              */
#define UFSD_RC_NOFILE      28   /* file or path not found           */
#define UFSD_RC_EXIST       32   /* file/dir already exists          */
#define UFSD_RC_NOTDIR      36   /* not a directory                  */
#define UFSD_RC_ISDIR       40   /* is a directory (not a file)      */
#define UFSD_RC_NOSPACE     44   /* no free disk blocks              */
#define UFSD_RC_NOINODES    48   /* no free inodes                   */
#define UFSD_RC_IO          52   /* I/O error                        */
#define UFSD_RC_BADFD       56   /* bad file descriptor              */
#define UFSD_RC_NOTEMPTY    60   /* directory not empty              */
#define UFSD_RC_NAMETOOLONG 64   /* filename exceeds max length      */
#define UFSD_RC_ROFS        68   /* read-only filesystem             */
#define UFSD_RC_EACCES      72   /* permission denied (owner check)  */

#endif /* UFSDRC_H */
