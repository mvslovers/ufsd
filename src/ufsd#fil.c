/* UFSD#FIL.C - File Operation Dispatch
**
** AP-1e: Server-side implementation of all file operations.
** Called from ufsd_dispatch (ufsd#que.c) after session lookup.
**
** AP-1f: Added UFSREQ_GETCWD handler (do_getcwd).
**
** AP-1f: Added UFSREQ_GETCWD handler (do_getcwd) and
**        UFSREQ_DIROPEN/DIRREAD/DIRCLOSE handlers.
**
** AP-1e Simplifications (all intentional for Phase 1 PoC):
**   - Direct blocks only (addr[0..15]); indirect blocks deferred
**   - No permission/ACEE checks
**   - Timestamps: mtime64() wallclock (AP-1g)
**   - Superblock freeblock cache refill deferred
**   - No inode caching (direct BDAM reads every time)
**   - fseek deferred
**
** Request/response data marshalling in req->data[] / resp_data[]:
**   FOPEN   req: [0..3]=mode(unsigned), [4..]=path(NUL-term)
**           rsp: [0..3]=fd(int, -1 on error)
**   FCLOSE  req: [0..3]=fd(int)
**   FREAD   req: [0..3]=fd(int), [4..7]=count(unsigned)
**           rsp: [0..3]=bytes_read(unsigned), [4..]=data
**   FWRITE  req: [0..3]=fd(int), [4..7]=count(unsigned), [8..]=data
**           rsp: [0..3]=bytes_written(unsigned)
**   MKDIR/RMDIR/CHGDIR/REMOVE
**           req: [0..]=path(NUL-terminated)
**           rsp: (none; rc in UFSSSOB.rc)
**   DIROPEN req: [0..]=path(NUL-term)
**           rsp: [0..3]=dir_fd(int)
**   DIRREAD req: [0..3]=dir_fd(int)
**           rsp: see UFSD_DIRREAD_RLEN (98 bytes), ino=0 means end
**   DIRCLOSE req: [0..3]=dir_fd(int)
**
** ufsd_fil_dispatch(UFSD@FDS)  main dispatch switch
*/

#include "ufsd.h"
#include <string.h>
#include <stdlib.h>
#include "time64.h"
#include "clib64.h"

/* ============================================================
** Internal helpers (static)
** ============================================================ */

/* ============================================================
** resolve_mount
**
** AP-3a: Find which mounted disk an absolute path belongs to.
** Returns the disk index with the longest matching mountpath.
** *out_path is set to the remainder of the path after the
** mount prefix (e.g. "/WWW/index.html" with mount "/WWW"
** yields out_path="index.html").
** For root mount "/" or no match, returns disk 0 and the
** original path.
** ============================================================ */
static int
resolve_mount(UFSD_STC *stc, const char *path, const char **out_path)
{
    unsigned i;
    int      best_idx;
    unsigned best_len;
    unsigned mlen;

    best_idx = 0;
    best_len = 1;  /* "/" always matches */

    for (i = 1; i < stc->ndisks; i++) {
        UFSD_DISK *d = stc->disks[i];
        if (!d || d->mountpath[0] == '\0') continue;
        mlen = strlen(d->mountpath);
        if (mlen <= best_len) continue;
        if (strncmp(path, d->mountpath, mlen) == 0
            && (path[mlen] == '/' || path[mlen] == '\0')) {
            best_idx = (int)i;
            best_len = mlen;
        }
    }

    if (best_len > 1) {
        /* Strip the mount prefix */
        *out_path = path + best_len;
        if (**out_path == '/') (*out_path)++;
    } else {
        *out_path = path;
    }
    return best_idx;
}

/* ============================================================
** resolve_path_disk
**
** Resolve the target disk and start inode for a path.
** For absolute paths: mount resolution via resolve_mount().
** For relative paths: use the session's current disk + cwd_ino.
**
** Sets *out_disk_idx, *out_start_ino, *out_path.
** Returns the UFSD_DISK pointer (or NULL on error).
** ============================================================ */
static UFSD_DISK *
resolve_path_disk(UFSD_STC *stc, UFSD_UFS *ufs, const char *path,
                  int *out_disk_idx, unsigned *out_start_ino,
                  const char **out_path)
{
    int didx;

    if (path[0] == '/') {
        didx           = resolve_mount(stc, path, out_path);
        *out_start_ino = UFSD_ROOT_INO;
    } else {
        didx       = ufs->disk_idx;
        *out_path  = path;
        *out_start_ino = ufs->cwd_ino;
    }

    *out_disk_idx = didx;
    if (didx < 0 || didx >= (int)stc->ndisks) return NULL;
    return stc->disks[didx];
}

/* Check whether a directory contains only "." and ".." entries.
** Returns 1 (empty) or 0 (not empty / I/O error). */
static int
dir_is_empty(UFSD_DISK *disk, unsigned dir_ino)
{
    UFSD_DINODE  dino;
    UFSD_DIRENT *de;
    char        *buf;
    unsigned     i;
    unsigned     j;
    unsigned     nde;
    unsigned     n_blocks;
    int          empty;

    if (ufsd_ino_read(disk, dir_ino, &dino) != UFSD_RC_OK) return 0;

    nde      = (unsigned)disk->blksize / UFSD_DIRENT_SIZE;
    n_blocks = (dino.filesize + (unsigned)disk->blksize - 1U)
               / (unsigned)disk->blksize;

    buf = (char *)malloc(disk->blksize);
    if (!buf) return 0;

    empty = 1;

    for (i = 0; i < UFSD_NADDR_DIRECT && i < n_blocks && empty; i++) {
        if (dino.addr[i] == 0) continue;
        if (ufsd_blk_read(disk, dino.addr[i], buf) != UFSD_RC_OK) continue;
        de = (UFSD_DIRENT *)buf;
        for (j = 0; j < nde; j++, de++) {
            if (de->ino == 0) continue;
            if (strcmp(de->name, ".") == 0) continue;
            if (strcmp(de->name, "..") == 0) continue;
            empty = 0;
            break;
        }
    }

    free(buf);
    return empty;
}

/* ============================================================
** Indirect block helpers
**
** addr[0..15]  = direct block addresses
** addr[16]     = single indirect (block of UINT32 addresses)
** addr[17..18] = double/triple indirect (not implemented)
**
** With 4K blocks: 1024 entries per indirect block.
** Max file size with single indirect: 16*4K + 1024*4K = 4.06 MB.
** ============================================================ */

#define UFSD_NADDR_SINDIRECT  16U  /* index of single indirect pointer */

/* Resolve logical block index to physical block number.
** Returns physical block number, or 0 if not allocated. */
static unsigned
blk_resolve(UFSD_DISK *disk, UFSD_DINODE *dino, unsigned blk_idx)
{
    unsigned *ind;
    char     *ibuf;
    unsigned  phys;
    unsigned  ind_off;

    if (blk_idx < UFSD_NADDR_DIRECT)
        return dino->addr[blk_idx];

    /* Single indirect */
    if (blk_idx < UFSD_NADDR_DIRECT + (unsigned)disk->blksize / 4U) {
        if (dino->addr[UFSD_NADDR_SINDIRECT] == 0) return 0;

        ibuf = (char *)malloc(disk->blksize);
        if (!ibuf) return 0;
        if (ufsd_blk_read(disk, dino->addr[UFSD_NADDR_SINDIRECT], ibuf)
            != UFSD_RC_OK) {
            free(ibuf);
            return 0;
        }
        ind     = (unsigned *)ibuf;
        ind_off = blk_idx - UFSD_NADDR_DIRECT;
        phys    = ind[ind_off];
        free(ibuf);
        return phys;
    }

    return 0;  /* double/triple indirect not implemented */
}

/* Allocate a data block for logical index blk_idx.
** Handles indirect block allocation if needed.
** On success, stores the physical block number in *out_sector,
** sets *sb_dirty = 1, and returns UFSD_RC_OK.
** The caller must zero the data block if it is new. */
static int
blk_alloc_at(UFSD_DISK *disk, UFSD_DINODE *dino,
             unsigned blk_idx, unsigned *out_sector, int *sb_dirty)
{
    unsigned  ind_blk;
    unsigned  new_blk;
    unsigned  ind_off;
    unsigned *ind;
    char     *ibuf;
    int       rc;

    if (blk_idx < UFSD_NADDR_DIRECT) {
        /* Direct block */
        if (ufsd_sb_alloc_block(disk, &new_blk) != UFSD_RC_OK)
            return UFSD_RC_NOSPACE;
        dino->addr[blk_idx] = new_blk;
        *out_sector = new_blk;
        *sb_dirty   = 1;
        return UFSD_RC_OK;
    }

    /* Single indirect */
    if (blk_idx >= UFSD_NADDR_DIRECT + (unsigned)disk->blksize / 4U)
        return UFSD_RC_NOSPACE;  /* beyond single indirect range */

    ind_off = blk_idx - UFSD_NADDR_DIRECT;

    ibuf = (char *)malloc(disk->blksize);
    if (!ibuf) return UFSD_RC_IO;

    /* Allocate indirect block itself if it does not exist yet */
    if (dino->addr[UFSD_NADDR_SINDIRECT] == 0) {
        if (ufsd_sb_alloc_block(disk, &ind_blk) != UFSD_RC_OK) {
            free(ibuf);
            return UFSD_RC_NOSPACE;
        }
        memset(ibuf, 0, disk->blksize);
        dino->addr[UFSD_NADDR_SINDIRECT] = ind_blk;
        *sb_dirty = 1;
    } else {
        if (ufsd_blk_read(disk, dino->addr[UFSD_NADDR_SINDIRECT], ibuf)
            != UFSD_RC_OK) {
            free(ibuf);
            return UFSD_RC_IO;
        }
    }

    /* Allocate the data block */
    if (ufsd_sb_alloc_block(disk, &new_blk) != UFSD_RC_OK) {
        free(ibuf);
        return UFSD_RC_NOSPACE;
    }

    ind = (unsigned *)ibuf;
    ind[ind_off] = new_blk;

    /* Write updated indirect block back */
    rc = ufsd_blk_write(disk, dino->addr[UFSD_NADDR_SINDIRECT], ibuf);
    free(ibuf);
    if (rc != UFSD_RC_OK) return UFSD_RC_IO;

    *out_sector = new_blk;
    *sb_dirty   = 1;
    return UFSD_RC_OK;
}

/* Free all data blocks of an inode, including indirect blocks.
** Does NOT free the inode itself. */
static void
blk_free_all(UFSD_DISK *disk, UFSD_DINODE *dino)
{
    unsigned  i;
    unsigned  nind;
    unsigned *ind;
    char     *ibuf;

    /* Free direct blocks */
    for (i = 0; i < UFSD_NADDR_DIRECT; i++) {
        if (dino->addr[i] != 0) {
            ufsd_sb_free_block(disk, dino->addr[i]);
            dino->addr[i] = 0;
        }
    }

    /* Free single indirect block and its data blocks */
    if (dino->addr[UFSD_NADDR_SINDIRECT] != 0) {
        ibuf = (char *)malloc(disk->blksize);
        if (ibuf) {
            if (ufsd_blk_read(disk, dino->addr[UFSD_NADDR_SINDIRECT], ibuf)
                == UFSD_RC_OK) {
                ind  = (unsigned *)ibuf;
                nind = (unsigned)disk->blksize / 4U;
                for (i = 0; i < nind; i++) {
                    if (ind[i] != 0)
                        ufsd_sb_free_block(disk, ind[i]);
                }
            }
            free(ibuf);
        }
        ufsd_sb_free_block(disk, dino->addr[UFSD_NADDR_SINDIRECT]);
        dino->addr[UFSD_NADDR_SINDIRECT] = 0;
    }
}

/* ============================================================
** ufsd_stamp
**
** Set ctime/mtime/atime on a UFSD_DINODE to current wallclock.
** Stores in v2 format (mtime64_t = milliseconds since epoch)
** to match ufs370's storage convention.
** ============================================================ */
static void
ufsd_stamp(UFSD_DINODE *dino)
{
    mtime64_t now;

    mtime64(&now);
    dino->ctime.v2 = now;
    dino->mtime.v2 = now;
    dino->atime.v2 = now;
}

/* ============================================================
** do_mkdir
**
** Create a new directory at path.
** Allocates one inode and one data block (for "." and "..").
** ============================================================ */
/* ============================================================
** ufsd_check_write
**
** AP-3a: Check whether a write operation is permitted on disk.
** Returns UFSD_RC_OK if allowed, UFSD_RC_ROFS if the mount is
** read-only, UFSD_RC_EACCES if the session owner does not match
** the mount owner.
** ============================================================ */
static int
ufsd_check_write(UFSD_DISK *disk, UFSD_SESSION *sess)
{
    if (disk->mount_mode == UFSD_MOUNT_RO)
        return UFSD_RC_ROFS;
    if (disk->mount_owner[0] != '\0'
        && strcmp(sess->owner, disk->mount_owner) != 0)
        return UFSD_RC_EACCES;
    return UFSD_RC_OK;
}

static int
do_mkdir(UFSD_STC *stc, UFSD_SESSION *sess,
         UFSREQ *req, char *resp_data, unsigned *resp_data_len)
{
    UFSD_UFS    *ufs;
    UFSD_DISK   *disk;
    UFSD_DINODE  dino;
    UFSD_DIRENT *de;
    const char  *path;
    const char  *mnt_path;
    char         dir_name[UFSD_NAME_MAX + 1];
    char        *blk;
    unsigned     parent_ino;
    unsigned     existing_ino;
    unsigned     new_ino;
    unsigned     new_blk;
    unsigned     start_ino;
    int          didx;
    int          rc;
    int          wrc;

    (void)resp_data;
    (void)resp_data_len;

    ufs = (UFSD_UFS *)sess->ufs;
    if (!ufs) return UFSD_RC_BADSESS;

    path = req->data;
    if (!path || path[0] == '\0') return UFSD_RC_INVALID;

    disk = resolve_path_disk(stc, ufs, path, &didx, &start_ino, &mnt_path);
    if (!disk) return UFSD_RC_IO;
    wrc = ufsd_check_write(disk, sess);
    if (wrc != UFSD_RC_OK) return wrc;

    parent_ino  = 0;
    dir_name[0] = '\0';
    existing_ino = ufsd_path_lookup(disk, start_ino,
        mnt_path, &parent_ino, dir_name);

    if (existing_ino != 0) return UFSD_RC_EXIST;
    if (parent_ino == 0 || dir_name[0] == '\0') return UFSD_RC_NOFILE;

    /* Guard: intermediate directory missing (see do_fopen comment) */
    {
        const char *slash = strrchr(mnt_path, '/');
        const char *base  = slash ? slash + 1 : mnt_path;
        if (strcmp(dir_name, base) != 0)
            return UFSD_RC_NOFILE;
    }

    if (ufsd_sb_alloc_inode(disk, &new_ino) != UFSD_RC_OK)
        return UFSD_RC_NOINODES;

    if (ufsd_sb_alloc_block(disk, &new_blk) != UFSD_RC_OK) {
        ufsd_sb_free_inode(disk, new_ino);
        return UFSD_RC_NOSPACE;
    }

    blk = (char *)calloc(1U, disk->blksize);
    if (!blk) {
        ufsd_sb_free_block(disk, new_blk);
        ufsd_sb_free_inode(disk, new_ino);
        return UFSD_RC_IO;
    }

    de       = (UFSD_DIRENT *)blk;
    de->ino  = new_ino;
    memset(de->name, 0, sizeof(de->name));
    memcpy(de->name, ".", 2);

    de++;
    de->ino  = parent_ino;
    memset(de->name, 0, sizeof(de->name));
    memcpy(de->name, "..", 3);

    rc = ufsd_blk_write(disk, new_blk, blk);
    free(blk);

    if (rc != UFSD_RC_OK) {
        ufsd_sb_free_block(disk, new_blk);
        ufsd_sb_free_inode(disk, new_ino);
        return UFSD_RC_IO;
    }

    memset(&dino, 0, sizeof(dino));
    dino.mode     = (unsigned short)(UFSD_IFDIR | 0755U);
    dino.nlink    = 2;
    dino.filesize = 2U * UFSD_DIRENT_SIZE;
    dino.addr[0]  = new_blk;
    ufsd_stamp(&dino);
    memcpy(dino.owner, sess->owner, 9);
    memcpy(dino.group, sess->group, 9);

    if (ufsd_ino_write(disk, new_ino, &dino) != UFSD_RC_OK) {
        ufsd_sb_free_block(disk, new_blk);
        ufsd_sb_free_inode(disk, new_ino);
        return UFSD_RC_IO;
    }

    rc = ufsd_dir_add(disk, parent_ino, dir_name, new_ino);
    if (rc != UFSD_RC_OK) {
        ufsd_sb_free_block(disk, new_blk);
        ufsd_sb_free_inode(disk, new_ino);
        return rc;
    }

    return ufsd_sb_write(disk);
}

/* ============================================================
** do_rmdir
**
** Remove an empty directory.
** ============================================================ */
static int
do_rmdir(UFSD_STC *stc, UFSD_SESSION *sess,
         UFSREQ *req, char *resp_data, unsigned *resp_data_len)
{
    UFSD_UFS    *ufs;
    UFSD_DISK   *disk;
    UFSD_DINODE  dino;
    const char  *path;
    const char  *mnt_path;
    char         dir_name[UFSD_NAME_MAX + 1];
    unsigned     parent_ino;
    unsigned     dir_ino;
    unsigned     start_ino;
    int          didx;
    int          rc;
    int          wrc;

    (void)resp_data;
    (void)resp_data_len;

    ufs = (UFSD_UFS *)sess->ufs;
    if (!ufs) return UFSD_RC_BADSESS;

    path = req->data;
    if (!path || path[0] == '\0') return UFSD_RC_INVALID;

    disk = resolve_path_disk(stc, ufs, path, &didx, &start_ino, &mnt_path);
    if (!disk) return UFSD_RC_IO;
    wrc = ufsd_check_write(disk, sess);
    if (wrc != UFSD_RC_OK) return wrc;

    parent_ino  = 0;
    dir_name[0] = '\0';
    dir_ino = ufsd_path_lookup(disk, start_ino,
        mnt_path, &parent_ino, dir_name);

    if (dir_ino == 0) return UFSD_RC_NOFILE;
    if (parent_ino == 0) return UFSD_RC_INVALID;  /* cannot remove root */

    if (ufsd_ino_read(disk, dir_ino, &dino) != UFSD_RC_OK) return UFSD_RC_IO;
    if ((dino.mode & UFSD_IFMT) != UFSD_IFDIR) return UFSD_RC_NOTDIR;

    if (!dir_is_empty(disk, dir_ino)) return UFSD_RC_NOTEMPTY;

    rc = ufsd_dir_remove(disk, parent_ino, dir_name);
    if (rc != UFSD_RC_OK) return rc;

    blk_free_all(disk, &dino);
    ufsd_sb_free_inode(disk, dir_ino);

    return ufsd_sb_write(disk);
}

/* ============================================================
** do_chgdir
**
** Change the session's current working directory.
** ============================================================ */
static int
do_chgdir(UFSD_STC *stc, UFSD_SESSION *sess,
          UFSREQ *req, char *resp_data, unsigned *resp_data_len)
{
    UFSD_UFS    *ufs;
    UFSD_DISK   *disk;
    UFSD_DINODE  dino;
    const char  *path;
    const char  *mnt_path;
    unsigned     dir_ino;
    unsigned     start_ino;
    unsigned     pathlen;
    int          didx;

    (void)resp_data;
    (void)resp_data_len;

    ufs = (UFSD_UFS *)sess->ufs;
    if (!ufs) return UFSD_RC_BADSESS;

    path = req->data;
    if (!path || path[0] == '\0') return UFSD_RC_INVALID;

    disk = resolve_path_disk(stc, ufs, path, &didx, &start_ino, &mnt_path);
    if (!disk) return UFSD_RC_IO;

    dir_ino = ufsd_path_lookup(disk, start_ino,
        mnt_path, NULL, NULL);

    if (dir_ino == 0) return UFSD_RC_NOFILE;
    if (ufsd_ino_read(disk, dir_ino, &dino) != UFSD_RC_OK) return UFSD_RC_IO;
    if ((dino.mode & UFSD_IFMT) != UFSD_IFDIR) return UFSD_RC_NOTDIR;

    ufs->cwd_ino  = dir_ino;
    ufs->disk_idx = didx;
    pathlen = strlen(path);
    if (pathlen >= sizeof(ufs->cwd))
        pathlen = sizeof(ufs->cwd) - 1U;
    memcpy(ufs->cwd, path, pathlen);
    ufs->cwd[pathlen] = '\0';

    return UFSD_RC_OK;
}

/* ============================================================
** do_remove
**
** Remove a regular file.
** ============================================================ */
static int
do_remove(UFSD_STC *stc, UFSD_SESSION *sess,
          UFSREQ *req, char *resp_data, unsigned *resp_data_len)
{
    UFSD_UFS    *ufs;
    UFSD_DISK   *disk;
    UFSD_DINODE  dino;
    const char  *path;
    const char  *mnt_path;
    char         file_name[UFSD_NAME_MAX + 1];
    unsigned     parent_ino;
    unsigned     file_ino;
    unsigned     start_ino;
    int          didx;
    int          rc;
    int          wrc;

    (void)resp_data;
    (void)resp_data_len;

    ufs = (UFSD_UFS *)sess->ufs;
    if (!ufs) return UFSD_RC_BADSESS;

    path = req->data;
    if (!path || path[0] == '\0') return UFSD_RC_INVALID;

    disk = resolve_path_disk(stc, ufs, path, &didx, &start_ino, &mnt_path);
    if (!disk) return UFSD_RC_IO;
    wrc = ufsd_check_write(disk, sess);
    if (wrc != UFSD_RC_OK) return wrc;

    parent_ino   = 0;
    file_name[0] = '\0';
    file_ino = ufsd_path_lookup(disk, start_ino,
        mnt_path, &parent_ino, file_name);

    if (file_ino == 0) return UFSD_RC_NOFILE;
    if (parent_ino == 0) return UFSD_RC_INVALID;

    if (ufsd_ino_read(disk, file_ino, &dino) != UFSD_RC_OK) return UFSD_RC_IO;
    if ((dino.mode & UFSD_IFMT) == UFSD_IFDIR) return UFSD_RC_ISDIR;

    rc = ufsd_dir_remove(disk, parent_ino, file_name);
    if (rc != UFSD_RC_OK) return rc;

    if (dino.nlink > 0) dino.nlink--;
    if (dino.nlink == 0) {
        blk_free_all(disk, &dino);
        ufsd_sb_free_inode(disk, file_ino);
    } else {
        ufsd_ino_write(disk, file_ino, &dino);
    }

    return ufsd_sb_write(disk);
}

/* ============================================================
** do_fopen
**
** Open (or create) a file.
** UFSD_OPEN_WRITE: create if absent, truncate if present.
** UFSD_OPEN_READ:  file must exist.
** Returns fd number (>=0) in resp_data[0..3] on success.
** ============================================================ */
static int
do_fopen(UFSD_STC *stc, UFSD_ANCHOR *anchor, UFSD_SESSION *sess,
         UFSREQ *req, char *resp_data, unsigned *resp_data_len)
{
    UFSD_UFS    *ufs;
    UFSD_DISK   *disk;
    UFSD_DINODE  dino;
    UFSD_GFILE  *gfile;
    const char  *path;
    const char  *mnt_path;
    char         file_name[UFSD_NAME_MAX + 1];
    unsigned     mode;
    unsigned     parent_ino;
    unsigned     file_ino;
    unsigned     new_ino;
    unsigned     start_ino;
    unsigned     gidx;
    unsigned     fd;
    int          didx;
    int          j;
    int          rc;
    int          wrc;

    ufs = (UFSD_UFS *)sess->ufs;
    if (!ufs) return UFSD_RC_BADSESS;

    if (req->data_len < 5U) return UFSD_RC_INVALID;
    mode = *(unsigned *)req->data;
    path = req->data + 4;

    disk = resolve_path_disk(stc, ufs, path, &didx, &start_ino, &mnt_path);
    if (!disk) return UFSD_RC_IO;

    parent_ino   = 0;
    file_name[0] = '\0';
    file_ino = ufsd_path_lookup(disk, start_ino,
        mnt_path, &parent_ino, file_name);

    new_ino = 0;
    rc      = UFSD_RC_OK;

    if (mode & UFSD_OPEN_WRITE) {
        wrc = ufsd_check_write(disk, sess);
        if (wrc != UFSD_RC_OK) return wrc;
        if (file_ino != 0) {
            /* File exists: truncate */
            if (ufsd_ino_read(disk, file_ino, &dino) != UFSD_RC_OK)
                return UFSD_RC_IO;
            if ((dino.mode & UFSD_IFMT) == UFSD_IFDIR) return UFSD_RC_ISDIR;
            blk_free_all(disk, &dino);
            dino.filesize = 0;
            ufsd_stamp(&dino);
            if (ufsd_ino_write(disk, file_ino, &dino) != UFSD_RC_OK)
                return UFSD_RC_IO;
            if (ufsd_sb_write(disk) != UFSD_RC_OK) return UFSD_RC_IO;
            new_ino = file_ino;
        } else {
            /* Create new file.
            ** Guard: if path_lookup stopped at an intermediate component
            ** (e.g. "/test/bar" where "test" doesn't exist), file_name
            ** will be "test" — not the final component "bar".  Detect
            ** this by comparing file_name with the basename of path. */
            const char *slash = strrchr(mnt_path, '/');
            const char *base  = slash ? slash + 1 : mnt_path;
            if (parent_ino == 0 || file_name[0] == '\0')
                return UFSD_RC_NOFILE;
            if (strcmp(file_name, base) != 0)
                return UFSD_RC_NOFILE;  /* intermediate dir missing */
            if (ufsd_sb_alloc_inode(disk, &new_ino) != UFSD_RC_OK)
                return UFSD_RC_NOINODES;
            memset(&dino, 0, sizeof(dino));
            dino.mode  = (unsigned short)(UFSD_IFREG | 0644U);
            dino.nlink = 1;
            ufsd_stamp(&dino);
            memcpy(dino.owner, sess->owner, 9);
            memcpy(dino.group, sess->group, 9);
            if (ufsd_ino_write(disk, new_ino, &dino) != UFSD_RC_OK) {
                ufsd_sb_free_inode(disk, new_ino);
                return UFSD_RC_IO;
            }
            rc = ufsd_dir_add(disk, parent_ino, file_name, new_ino);
            if (rc != UFSD_RC_OK) {
                ufsd_sb_free_inode(disk, new_ino);
                return rc;
            }
            if (ufsd_sb_write(disk) != UFSD_RC_OK) return UFSD_RC_IO;
        }
    } else {
        /* Read mode: file must exist */
        if (file_ino == 0) return UFSD_RC_NOFILE;
        if (ufsd_ino_read(disk, file_ino, &dino) != UFSD_RC_OK)
            return UFSD_RC_IO;
        if ((dino.mode & UFSD_IFMT) == UFSD_IFDIR) return UFSD_RC_ISDIR;
        new_ino = file_ino;
    }

    /* Allocate GFT entry */
    if (ufsd_gft_alloc(anchor, &gidx) != UFSD_RC_OK)
        return UFSD_RC_NOREQ;

    gfile            = &anchor->gfiles[gidx];
    gfile->flags     = UFSD_GF_USED;
    gfile->disk_idx  = didx;
    gfile->ino       = new_ino;
    gfile->position  = 0;
    gfile->open_mode = mode;
    gfile->refcount  = 1;

    /* Find first free fd in session */
    fd = (unsigned)UFSD_MAX_FD;
    for (j = 0; j < UFSD_MAX_FD; j++) {
        if (sess->fd_table[j].gfile_idx == UFSD_FD_UNUSED) {
            fd = (unsigned)j;
            break;
        }
    }
    if (fd >= (unsigned)UFSD_MAX_FD) {
        ufsd_gft_release(anchor, gidx);
        return UFSD_RC_NOREQ;
    }

    sess->fd_table[fd].gfile_idx = gidx;
    sess->fd_table[fd].flags     = mode;

    *(int *)resp_data = (int)fd;
    *resp_data_len    = sizeof(int);
    return UFSD_RC_OK;
}

/* ============================================================
** do_fclose
**
** Release GFT entry and mark fd as unused.
** ============================================================ */
static int
do_fclose(UFSD_STC *stc, UFSD_ANCHOR *anchor, UFSD_SESSION *sess,
          UFSREQ *req, char *resp_data, unsigned *resp_data_len)
{
    int      fd;
    unsigned gidx;

    (void)stc;
    (void)resp_data;
    (void)resp_data_len;

    if (req->data_len < 4U) return UFSD_RC_INVALID;
    fd = (int)*(unsigned *)req->data;

    if (fd < 0 || fd >= UFSD_MAX_FD) return UFSD_RC_BADFD;
    if (sess->fd_table[fd].gfile_idx == UFSD_FD_UNUSED) return UFSD_RC_BADFD;

    gidx = sess->fd_table[fd].gfile_idx;
    ufsd_gft_release(anchor, gidx);
    sess->fd_table[fd].gfile_idx = UFSD_FD_UNUSED;
    sess->fd_table[fd].flags     = 0;

    return UFSD_RC_OK;
}

/* ============================================================
** do_fread
**
** Read up to 'count' bytes from the file at the current position.
** Response: resp_data[0..3]=bytes_read, resp_data[4..]=data.
** ============================================================ */
static int
do_fread(UFSD_STC *stc, UFSD_ANCHOR *anchor, UFSD_SESSION *sess,
         UFSREQ *req, char *resp_data, unsigned *resp_data_len)
{
    UFSD_DISK   *disk;
    UFSD_GFILE  *gfile;
    UFSD_DINODE  dino;
    char        *blk;
    char        *dst;
    char        *staging;
    int          fd;
    unsigned     count;
    unsigned     gidx;
    unsigned     blk_idx;
    unsigned     blk_off;
    unsigned     chunk;
    unsigned     bytes_read;
    int          rc;

    if (req->data_len < 8U) return UFSD_RC_INVALID;
    fd    = (int)*(unsigned *)req->data;
    count = *(unsigned *)(req->data + 4);

    if (fd < 0 || fd >= UFSD_MAX_FD) return UFSD_RC_BADFD;
    if (sess->fd_table[fd].gfile_idx == UFSD_FD_UNUSED) return UFSD_RC_BADFD;

    gidx  = sess->fd_table[fd].gfile_idx;
    gfile = &anchor->gfiles[gidx];
    if (!(gfile->flags & UFSD_GF_USED)) return UFSD_RC_BADFD;
    if (!(gfile->open_mode & UFSD_OPEN_READ)) return UFSD_RC_INVALID;

    disk = stc->disks[gfile->disk_idx];
    if (!disk) return UFSD_RC_IO;

    /*
    ** Buffer selection: when count > inline max, allocate a heap staging
    ** buffer and read into it.  ufsd_dispatch will copy to a CSA pool
    ** buffer in its key-0 window.  If malloc fails, fall back to the
    ** inline path (clamped to 252 bytes).
    **
    ** do_fread never touches CSA directly -- all CSA writes happen in
    ** ufsd_dispatch's existing key-0 block.
    */
    staging = NULL;
    if (count > UFSREQ_MAX_INLINE - 4U) {
        if (count > 4096U) count = 4096U;
        staging = (char *)malloc(count);
        if (staging) {
            dst = staging;
        } else {
            count = UFSREQ_MAX_INLINE - 4U;
            dst = resp_data + 4;
        }
    } else {
        dst = resp_data + 4;
    }

    rc = ufsd_ino_read(disk, gfile->ino, &dino);
    if (rc != UFSD_RC_OK) return rc;

    blk = (char *)malloc(disk->blksize);
    if (!blk) return UFSD_RC_IO;

    bytes_read = 0;
    rc         = UFSD_RC_OK;

    while (bytes_read < count && gfile->position < dino.filesize) {
        unsigned phys;

        blk_idx = gfile->position / (unsigned)disk->blksize;
        blk_off = gfile->position % (unsigned)disk->blksize;

        phys = blk_resolve(disk, &dino, blk_idx);
        if (phys == 0) break;

        if (ufsd_blk_read(disk, phys, blk) != UFSD_RC_OK) {
            rc = UFSD_RC_IO;
            break;
        }

        chunk = (unsigned)disk->blksize - blk_off;
        if (gfile->position + chunk > dino.filesize)
            chunk = dino.filesize - gfile->position;
        if (chunk > count - bytes_read)
            chunk = count - bytes_read;

        memcpy(dst + bytes_read, blk + blk_off, chunk);
        bytes_read      += chunk;
        gfile->position += chunk;
    }

    free(blk);

    *(unsigned *)resp_data = bytes_read;
    if (staging != NULL) {
        /* 4K path: pass staging pointer in resp_data[4..7].
        ** ufsd_dispatch copies to CSA buffer in key-0 and frees staging. */
        *(char **)(resp_data + 4) = staging;
        *resp_data_len = 4U;
    } else {
        /* Inline path: bytes_read + data both in resp_data */
        *resp_data_len = 4U + bytes_read;
    }
    return rc;
}

/* ============================================================
** do_fwrite
**
** Write 'count' bytes to the file at the current position,
** allocating new data blocks as needed.
** Response: resp_data[0..3]=bytes_written.
** ============================================================ */
static int
do_fwrite(UFSD_STC *stc, UFSD_ANCHOR *anchor, UFSD_SESSION *sess,
          UFSREQ *req, char *resp_data, unsigned *resp_data_len)
{
    UFSD_DISK      *disk;
    UFSD_GFILE     *gfile;
    UFSD_DINODE     dino;
    char           *blk;
    const char     *src;
    int             fd;
    unsigned        count;
    unsigned        bytes_written;
    unsigned        gidx;
    unsigned        blk_idx;
    unsigned        blk_off;
    unsigned        chunk;
    unsigned        sector;
    int             rc;
    int             sb_dirty;
    int             wrc;

    if (req->data_len < 8U) return UFSD_RC_INVALID;
    fd    = (int)*(unsigned *)req->data;
    count = *(unsigned *)(req->data + 4);

    /* 4K path: data is in req->buf->data; inline path: data in req->data+8 */
    if (req->buf != NULL)
        src = req->buf->data;
    else
        src = req->data + 8;

    if (fd < 0 || fd >= UFSD_MAX_FD) return UFSD_RC_BADFD;
    if (sess->fd_table[fd].gfile_idx == UFSD_FD_UNUSED) return UFSD_RC_BADFD;

    if (count == 0) {
        *(unsigned *)resp_data = 0;
        *resp_data_len         = 4U;
        return UFSD_RC_OK;
    }
    if (req->buf == NULL && req->data_len < 8U + count) return UFSD_RC_INVALID;

    gidx  = sess->fd_table[fd].gfile_idx;
    gfile = &anchor->gfiles[gidx];
    if (!(gfile->flags & UFSD_GF_USED)) return UFSD_RC_BADFD;
    if (!(gfile->open_mode & UFSD_OPEN_WRITE)) return UFSD_RC_INVALID;

    disk = stc->disks[gfile->disk_idx];
    if (!disk) return UFSD_RC_IO;
    wrc = ufsd_check_write(disk, sess);
    if (wrc != UFSD_RC_OK) return wrc;

    rc = ufsd_ino_read(disk, gfile->ino, &dino);
    if (rc != UFSD_RC_OK) return rc;

    blk = (char *)malloc(disk->blksize);
    if (!blk) return UFSD_RC_IO;

    bytes_written = 0;
    rc            = UFSD_RC_OK;
    sb_dirty      = 0;

    while (bytes_written < count) {
        blk_idx = gfile->position / (unsigned)disk->blksize;
        blk_off = gfile->position % (unsigned)disk->blksize;

        sector = blk_resolve(disk, &dino, blk_idx);
        if (sector == 0) {
            /* Allocate new data block (direct or indirect) */
            rc = blk_alloc_at(disk, &dino, blk_idx, &sector, &sb_dirty);
            if (rc != UFSD_RC_OK) break;
            memset(blk, 0, disk->blksize);
        } else {
            if (ufsd_blk_read(disk, sector, blk) != UFSD_RC_OK) {
                rc = UFSD_RC_IO;
                break;
            }
        }

        chunk = (unsigned)disk->blksize - blk_off;
        if (chunk > count - bytes_written)
            chunk = count - bytes_written;

        memcpy(blk + blk_off, src + bytes_written, chunk);

        if (ufsd_blk_write(disk, sector, blk) != UFSD_RC_OK) {
            rc = UFSD_RC_IO;
            break;
        }

        bytes_written   += chunk;
        gfile->position += chunk;
    }

    free(blk);

    /* Update inode filesize and mtime after successful write */
    if (bytes_written > 0) {
        if (gfile->position > dino.filesize)
            dino.filesize = gfile->position;
        ufsd_stamp(&dino);
        ufsd_ino_write(disk, gfile->ino, &dino);
        if (sb_dirty)
            ufsd_sb_write(disk);
    }

    *(unsigned *)resp_data = bytes_written;
    *resp_data_len         = 4U;
    return rc;
}

/* ============================================================
** do_getcwd
**
** AP-1f: Return the session's current working directory as a
** NUL-terminated string in resp_data[].
** ============================================================ */
static int
do_getcwd(UFSD_STC *stc, UFSD_SESSION *sess,
          UFSREQ *req, char *resp_data, unsigned *resp_data_len)
{
    UFSD_UFS *ufs;
    unsigned  pathlen;

    (void)stc;
    (void)req;

    ufs = (UFSD_UFS *)sess->ufs;
    if (!ufs) return UFSD_RC_BADSESS;

    pathlen = strlen(ufs->cwd);
    if (pathlen >= (unsigned)UFSREQ_MAX_INLINE)
        pathlen = (unsigned)UFSREQ_MAX_INLINE - 1U;

    memcpy(resp_data, ufs->cwd, pathlen + 1U);
    *resp_data_len = pathlen + 1U;

    return UFSD_RC_OK;
}

/* ============================================================
** do_diropen
**
** AP-1f: Open a directory for listing.  Resolves path to inode,
** verifies it is a directory, allocates a GFT entry with the
** UFSD_GF_DIR flag, and assigns a session fd slot.
**
** Response: resp_data[0..3] = dir_fd (int).
** ============================================================ */
static int
do_diropen(UFSD_STC *stc, UFSD_ANCHOR *anchor, UFSD_SESSION *sess,
           UFSREQ *req, char *resp_data, unsigned *resp_data_len)
{
    UFSD_UFS    *ufs;
    UFSD_DISK   *disk;
    UFSD_DINODE  dino;
    UFSD_GFILE  *gfile;
    const char  *path;
    const char  *mnt_path;
    unsigned     dir_ino;
    unsigned     start_ino;
    unsigned     gidx;
    unsigned     fd;
    int          didx;
    int          j;

    ufs = (UFSD_UFS *)sess->ufs;
    if (!ufs) return UFSD_RC_BADSESS;

    path = req->data;
    if (!path || path[0] == '\0') return UFSD_RC_INVALID;

    disk = resolve_path_disk(stc, ufs, path, &didx, &start_ino, &mnt_path);
    if (!disk) return UFSD_RC_IO;

    dir_ino = ufsd_path_lookup(disk, start_ino,
        mnt_path, NULL, NULL);
    if (dir_ino == 0) return UFSD_RC_NOFILE;

    if (ufsd_ino_read(disk, dir_ino, &dino) != UFSD_RC_OK)
        return UFSD_RC_IO;
    if ((dino.mode & UFSD_IFMT) != UFSD_IFDIR) return UFSD_RC_NOTDIR;

    if (ufsd_gft_alloc(anchor, &gidx) != UFSD_RC_OK) return UFSD_RC_NOREQ;

    gfile            = &anchor->gfiles[gidx];
    gfile->flags     = UFSD_GF_USED | UFSD_GF_DIR;
    gfile->disk_idx  = didx;
    gfile->ino       = dir_ino;
    gfile->position  = 0;
    gfile->open_mode = UFSD_OPEN_READ;
    gfile->refcount  = 1;

    fd = (unsigned)UFSD_MAX_FD;
    for (j = 0; j < UFSD_MAX_FD; j++) {
        if (sess->fd_table[j].gfile_idx == UFSD_FD_UNUSED) {
            fd = (unsigned)j;
            break;
        }
    }
    if (fd >= (unsigned)UFSD_MAX_FD) {
        ufsd_gft_release(anchor, gidx);
        return UFSD_RC_NOREQ;
    }

    sess->fd_table[fd].gfile_idx = gidx;
    sess->fd_table[fd].flags     = UFSD_FD_READ;

    *(int *)resp_data = (int)fd;
    *resp_data_len    = sizeof(int);
    return UFSD_RC_OK;
}

/* ============================================================
** do_dirread
**
** AP-1f: Return the next non-deleted directory entry.
** gfile->position tracks which entry index to start from
** (counting ALL slots including deleted ones).
**
** Response when entry found: UFSD_DIRREAD_RLEN bytes.
** Response at end of dir: 4 bytes, ino field = 0.
** ============================================================ */
static int
do_dirread(UFSD_STC *stc, UFSD_ANCHOR *anchor, UFSD_SESSION *sess,
           UFSREQ *req, char *resp_data, unsigned *resp_data_len)
{
    UFSD_DISK   *disk;
    UFSD_GFILE  *gfile;
    UFSD_DINODE  dino;
    UFSD_DINODE  edino;
    UFSD_DIRENT *de;
    char        *blk;
    int          fd;
    unsigned     gidx;
    unsigned     blk_size;
    unsigned     nde;
    unsigned     n_blocks;
    unsigned     entry_pos;
    unsigned     found_ino;
    unsigned     i;
    unsigned     j;

    if (req->data_len < 4U) return UFSD_RC_INVALID;
    fd = (int)*(unsigned *)req->data;

    if (fd < 0 || fd >= UFSD_MAX_FD) return UFSD_RC_BADFD;
    if (sess->fd_table[fd].gfile_idx == UFSD_FD_UNUSED) return UFSD_RC_BADFD;

    gidx  = sess->fd_table[fd].gfile_idx;
    gfile = &anchor->gfiles[gidx];
    if (!(gfile->flags & UFSD_GF_USED)) return UFSD_RC_BADFD;
    if (!(gfile->flags & UFSD_GF_DIR))  return UFSD_RC_BADFD;

    disk = stc->disks[gfile->disk_idx];
    if (!disk) return UFSD_RC_IO;

    if (ufsd_ino_read(disk, gfile->ino, &dino) != UFSD_RC_OK)
        return UFSD_RC_IO;

    blk_size = (unsigned)disk->blksize;
    nde      = blk_size / UFSD_DIRENT_SIZE;
    n_blocks = (dino.filesize + blk_size - 1U) / blk_size;

    blk = (char *)malloc(blk_size);
    if (!blk) return UFSD_RC_IO;

    found_ino = 0;
    entry_pos = 0;

    for (i = 0; i < UFSD_NADDR_DIRECT && i < n_blocks && found_ino == 0; i++) {
        if (dino.addr[i] == 0) {
            entry_pos += nde;
            continue;
        }
        if (ufsd_blk_read(disk, dino.addr[i], blk) != UFSD_RC_OK) {
            entry_pos += nde;
            continue;
        }
        de = (UFSD_DIRENT *)blk;
        for (j = 0; j < nde && found_ino == 0; j++, de++, entry_pos++) {
            if (entry_pos < gfile->position) continue;
            if (de->ino == 0) continue;

            found_ino       = de->ino;
            gfile->position = entry_pos + 1U;

            /* Mount boundary: if this is ".." at the root of a
            ** mounted filesystem, resolve the parent from the
            ** mount point on the parent disk instead. */
            if (de->name[0] == '.' && de->name[1] == '.'
                && de->name[2] == '\0'
                && gfile->ino == UFSD_ROOT_INO
                && gfile->disk_idx > 0) {
                UFSD_DISK *parent_disk = stc->disks[0];
                if (parent_disk) {
                    const char *mpath = disk->mountpath;
                    unsigned    pino;
                    char        pname[UFSD_NAME_MAX + 1];
                    unsigned    ppino = 0;
                    pino = ufsd_path_lookup(parent_disk, UFSD_ROOT_INO,
                               mpath, &ppino, pname);
                    if (ppino != 0) {
                        found_ino = ppino;
                        disk = parent_disk;
                    }
                }
            }

            memset(resp_data, 0, UFSD_DIRREAD_RLEN);
            *(unsigned *)resp_data = found_ino;

            if (ufsd_ino_read(disk, found_ino, &edino) == UFSD_RC_OK) {
                mtime64_t mt;

                *(unsigned *)(resp_data + 4)        = edino.filesize;
                *(unsigned short *)(resp_data + 8)  = edino.mode;
                *(unsigned short *)(resp_data + 10) = edino.nlink;

                /* v1/v2 timestamp detection (matches ufs370):
                ** v1: useconds < 1000000 -> sec+usec pair
                ** v2: raw mtime64_t across both fields */
                if (edino.mtime.v1.useconds < 1000000U) {
                    /* v1: convert seconds + usec to milliseconds */
                    __64_from_u32(&mt, edino.mtime.v1.seconds);
                    __64_mul_u32(&mt, 1000U, &mt);
                    __64_add_u32(&mt, edino.mtime.v1.useconds / 1000U, &mt);
                } else {
                    /* v2: already mtime64_t (milliseconds) */
                    mt = edino.mtime.v2;
                }
                memcpy(resp_data + 72, &mt, 8);

                memcpy(resp_data + 80, edino.owner, 9);
                memcpy(resp_data + 89, edino.group, 9);
            }
            memcpy(resp_data + 12, de->name, UFSD_NAME_MAX);
            resp_data[12 + UFSD_NAME_MAX] = '\0';
        }
    }

    free(blk);

    if (found_ino == 0) {
        *(unsigned *)resp_data = 0U;
        *resp_data_len         = 4U;
    } else {
        *resp_data_len = UFSD_DIRREAD_RLEN;
    }

    return UFSD_RC_OK;
}

/* ============================================================
** do_dirclose
**
** AP-1f: Release a directory handle (GFT entry + fd slot).
** ============================================================ */
static int
do_dirclose(UFSD_ANCHOR *anchor, UFSD_SESSION *sess,
            UFSREQ *req, char *resp_data, unsigned *resp_data_len)
{
    int      fd;
    unsigned gidx;

    (void)resp_data;
    (void)resp_data_len;

    if (req->data_len < 4U) return UFSD_RC_INVALID;
    fd = (int)*(unsigned *)req->data;

    if (fd < 0 || fd >= UFSD_MAX_FD) return UFSD_RC_BADFD;
    if (sess->fd_table[fd].gfile_idx == UFSD_FD_UNUSED) return UFSD_RC_BADFD;

    gidx = sess->fd_table[fd].gfile_idx;
    if (!(anchor->gfiles[gidx].flags & UFSD_GF_DIR)) return UFSD_RC_BADFD;

    ufsd_gft_release(anchor, gidx);
    sess->fd_table[fd].gfile_idx = UFSD_FD_UNUSED;
    sess->fd_table[fd].flags     = 0;

    return UFSD_RC_OK;
}

/* ============================================================
** ufsd_fil_dispatch
**
** Top-level dispatch for AP-1e file operations.
** Called from ufsd_dispatch (ufsd#que.c) after session lookup.
**
** req->data[]     holds the request parameters (read from CSA)
** resp_data[]     receives the response data (STC stack, key-8)
** *resp_data_len  receives the response data length
**
** Returns UFSD_RC_OK or an UFSD_RC_* error code.
** ============================================================ */
int
/* ============================================================
** do_stat
**
** Stat a file or directory by path. Returns inode metadata in
** the same wire format as DIRREAD (UFSD_DIRREAD_RLEN bytes).
** No file descriptor is allocated — lightweight metadata-only.
**
** Request:  data[0..] = path (NUL-terminated)
** Response: DIRREAD format (98 bytes), see UFSD_DIRREAD_RLEN.
** ============================================================ */
static int
do_stat(UFSD_STC *stc, UFSD_SESSION *sess,
        UFSREQ *req, char *resp_data, unsigned *resp_data_len)
{
    UFSD_UFS    *ufs;
    UFSD_DISK   *disk;
    UFSD_DINODE  dino;
    const char  *path;
    const char  *mnt_path;
    char         leaf_name[UFSD_NAME_MAX + 1];
    unsigned     start_ino;
    unsigned     ino;
    int          didx;
    mtime64_t    mt;

    ufs = (UFSD_UFS *)sess->ufs;
    if (!ufs) return UFSD_RC_BADSESS;

    path = req->data;
    if (!path || path[0] == '\0') return UFSD_RC_INVALID;

    disk = resolve_path_disk(stc, ufs, path, &didx, &start_ino, &mnt_path);
    if (!disk) return UFSD_RC_IO;

    ino = ufsd_path_lookup(disk, start_ino, mnt_path, NULL, leaf_name);
    if (ino == 0) return UFSD_RC_NOFILE;

    if (ufsd_ino_read(disk, ino, &dino) != UFSD_RC_OK)
        return UFSD_RC_IO;

    /* Root path returns empty leaf_name from path_lookup */
    if (leaf_name[0] == '\0') {
        leaf_name[0] = '/';
        leaf_name[1] = '\0';
    }

    memset(resp_data, 0, UFSD_DIRREAD_RLEN);

    *(unsigned *)resp_data                = ino;
    *(unsigned *)(resp_data + 4)          = dino.filesize;
    *(unsigned short *)(resp_data + 8)    = dino.mode;
    *(unsigned short *)(resp_data + 10)   = dino.nlink;

    memcpy(resp_data + 12, leaf_name, UFSD_NAME_MAX);
    resp_data[12 + UFSD_NAME_MAX] = '\0';

    /* v1/v2 timestamp detection (matches ufs370 / do_dirread) */
    if (dino.mtime.v1.useconds < 1000000U) {
        __64_from_u32(&mt, dino.mtime.v1.seconds);
        __64_mul_u32(&mt, 1000U, &mt);
        __64_add_u32(&mt, dino.mtime.v1.useconds / 1000U, &mt);
    } else {
        mt = dino.mtime.v2;
    }
    memcpy(resp_data + 72, &mt, 8);

    memcpy(resp_data + 80, dino.owner, 9);
    memcpy(resp_data + 89, dino.group, 9);

    *resp_data_len = UFSD_DIRREAD_RLEN;
    return UFSD_RC_OK;
}

int
ufsd_fil_dispatch(UFSD_ANCHOR *anchor, UFSD_SESSION *sess,
                  UFSREQ *req,
                  char *resp_data, unsigned *resp_data_len)
{
    UFSD_STC *stc;

    if (!anchor || !sess || !req) return UFSD_RC_CORRUPT;

    stc = (UFSD_STC *)anchor->server_stc;
    if (!stc) return UFSD_RC_CORRUPT;

    *resp_data_len = 0;

    switch (req->func) {

    case UFSREQ_MKDIR:
        return do_mkdir(stc, sess, req, resp_data, resp_data_len);

    case UFSREQ_RMDIR:
        return do_rmdir(stc, sess, req, resp_data, resp_data_len);

    case UFSREQ_CHGDIR:
        return do_chgdir(stc, sess, req, resp_data, resp_data_len);

    case UFSREQ_REMOVE:
        return do_remove(stc, sess, req, resp_data, resp_data_len);

    case UFSREQ_FOPEN:
        return do_fopen(stc, anchor, sess, req, resp_data, resp_data_len);

    case UFSREQ_FCLOSE:
        return do_fclose(stc, anchor, sess, req, resp_data, resp_data_len);

    case UFSREQ_FREAD:
        return do_fread(stc, anchor, sess, req, resp_data, resp_data_len);

    case UFSREQ_FWRITE:
        return do_fwrite(stc, anchor, sess, req, resp_data, resp_data_len);

    case UFSREQ_GETCWD:
        return do_getcwd(stc, sess, req, resp_data, resp_data_len);

    case UFSREQ_DIROPEN:
        return do_diropen(stc, anchor, sess, req, resp_data, resp_data_len);

    case UFSREQ_DIRREAD:
        return do_dirread(stc, anchor, sess, req, resp_data, resp_data_len);

    case UFSREQ_DIRCLOSE:
        return do_dirclose(anchor, sess, req, resp_data, resp_data_len);

    case UFSREQ_STAT:
        return do_stat(stc, sess, req, resp_data, resp_data_len);

    default:
        return UFSD_RC_NOTIMPL;
    }
}
