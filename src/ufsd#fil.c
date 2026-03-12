/* UFSD#FIL.C - File Operation Dispatch
**
** AP-1e: Server-side implementation of all file operations.
** Called from ufsd_dispatch (ufsd#que.c) after session lookup.
**
** AP-1e Simplifications (all intentional for Phase 1 PoC):
**   - Direct blocks only (addr[0..15]); indirect blocks deferred
**   - No permission/ACEE checks
**   - Timestamps written as 0
**   - Superblock freeblock cache refill deferred
**   - No inode caching (direct BDAM reads every time)
**   - diropen/dirread/dirclose deferred
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
**
** ufsd_fil_dispatch(UFSD@FDS)  main dispatch switch
*/

#include "ufsd.h"
#include <string.h>
#include <stdlib.h>

/* ============================================================
** Internal helpers (static)
** ============================================================ */

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
** do_mkdir
**
** Create a new directory at path.
** Allocates one inode and one data block (for "." and "..").
** ============================================================ */
static int
do_mkdir(UFSD_STC *stc, UFSD_SESSION *sess,
         UFSREQ *req, char *resp_data, unsigned *resp_data_len)
{
    UFSD_UFS    *ufs;
    UFSD_DISK   *disk;
    UFSD_DINODE  dino;
    UFSD_DIRENT *de;
    const char  *path;
    char         dir_name[UFSD_NAME_MAX + 1];
    char        *blk;
    unsigned     parent_ino;
    unsigned     existing_ino;
    unsigned     new_ino;
    unsigned     new_blk;
    int          rc;

    (void)resp_data;
    (void)resp_data_len;

    ufs = (UFSD_UFS *)sess->ufs;
    if (!ufs) return UFSD_RC_BADSESS;
    disk = stc->disks[ufs->disk_idx];
    if (!disk) return UFSD_RC_IO;

    path = req->data;
    if (!path || path[0] == '\0') return UFSD_RC_INVALID;

    parent_ino  = 0;
    dir_name[0] = '\0';
    existing_ino = ufsd_path_lookup(disk,
        (path[0] == '/') ? UFSD_ROOT_INO : ufs->cwd_ino,
        path, &parent_ino, dir_name);

    if (existing_ino != 0) return UFSD_RC_EXIST;
    if (parent_ino == 0 || dir_name[0] == '\0') return UFSD_RC_NOFILE;

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
    char         dir_name[UFSD_NAME_MAX + 1];
    unsigned     parent_ino;
    unsigned     dir_ino;
    unsigned     i;
    int          rc;

    (void)resp_data;
    (void)resp_data_len;

    ufs = (UFSD_UFS *)sess->ufs;
    if (!ufs) return UFSD_RC_BADSESS;
    disk = stc->disks[ufs->disk_idx];
    if (!disk) return UFSD_RC_IO;

    path = req->data;
    if (!path || path[0] == '\0') return UFSD_RC_INVALID;

    parent_ino  = 0;
    dir_name[0] = '\0';
    dir_ino = ufsd_path_lookup(disk,
        (path[0] == '/') ? UFSD_ROOT_INO : ufs->cwd_ino,
        path, &parent_ino, dir_name);

    if (dir_ino == 0) return UFSD_RC_NOFILE;
    if (parent_ino == 0) return UFSD_RC_INVALID;  /* cannot remove root */

    if (ufsd_ino_read(disk, dir_ino, &dino) != UFSD_RC_OK) return UFSD_RC_IO;
    if ((dino.mode & UFSD_IFMT) != UFSD_IFDIR) return UFSD_RC_NOTDIR;

    if (!dir_is_empty(disk, dir_ino)) return UFSD_RC_NOTEMPTY;

    rc = ufsd_dir_remove(disk, parent_ino, dir_name);
    if (rc != UFSD_RC_OK) return rc;

    for (i = 0; i < UFSD_NADDR_DIRECT; i++) {
        if (dino.addr[i] != 0)
            ufsd_sb_free_block(disk, dino.addr[i]);
    }
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
    unsigned     dir_ino;
    unsigned     pathlen;

    (void)resp_data;
    (void)resp_data_len;

    ufs = (UFSD_UFS *)sess->ufs;
    if (!ufs) return UFSD_RC_BADSESS;
    disk = stc->disks[ufs->disk_idx];
    if (!disk) return UFSD_RC_IO;

    path = req->data;
    if (!path || path[0] == '\0') return UFSD_RC_INVALID;

    dir_ino = ufsd_path_lookup(disk,
        (path[0] == '/') ? UFSD_ROOT_INO : ufs->cwd_ino,
        path, NULL, NULL);

    if (dir_ino == 0) return UFSD_RC_NOFILE;
    if (ufsd_ino_read(disk, dir_ino, &dino) != UFSD_RC_OK) return UFSD_RC_IO;
    if ((dino.mode & UFSD_IFMT) != UFSD_IFDIR) return UFSD_RC_NOTDIR;

    ufs->cwd_ino = dir_ino;
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
    char         file_name[UFSD_NAME_MAX + 1];
    unsigned     parent_ino;
    unsigned     file_ino;
    unsigned     i;
    int          rc;

    (void)resp_data;
    (void)resp_data_len;

    ufs = (UFSD_UFS *)sess->ufs;
    if (!ufs) return UFSD_RC_BADSESS;
    disk = stc->disks[ufs->disk_idx];
    if (!disk) return UFSD_RC_IO;

    path = req->data;
    if (!path || path[0] == '\0') return UFSD_RC_INVALID;

    parent_ino   = 0;
    file_name[0] = '\0';
    file_ino = ufsd_path_lookup(disk,
        (path[0] == '/') ? UFSD_ROOT_INO : ufs->cwd_ino,
        path, &parent_ino, file_name);

    if (file_ino == 0) return UFSD_RC_NOFILE;
    if (parent_ino == 0) return UFSD_RC_INVALID;

    if (ufsd_ino_read(disk, file_ino, &dino) != UFSD_RC_OK) return UFSD_RC_IO;
    if ((dino.mode & UFSD_IFMT) == UFSD_IFDIR) return UFSD_RC_ISDIR;

    rc = ufsd_dir_remove(disk, parent_ino, file_name);
    if (rc != UFSD_RC_OK) return rc;

    if (dino.nlink > 0) dino.nlink--;
    if (dino.nlink == 0) {
        for (i = 0; i < UFSD_NADDR_DIRECT; i++) {
            if (dino.addr[i] != 0)
                ufsd_sb_free_block(disk, dino.addr[i]);
        }
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
    char         file_name[UFSD_NAME_MAX + 1];
    unsigned     mode;
    unsigned     parent_ino;
    unsigned     file_ino;
    unsigned     new_ino;
    unsigned     gidx;
    unsigned     fd;
    unsigned     i;
    int          j;
    int          rc;

    ufs = (UFSD_UFS *)sess->ufs;
    if (!ufs) return UFSD_RC_BADSESS;
    disk = stc->disks[ufs->disk_idx];
    if (!disk) return UFSD_RC_IO;

    if (req->data_len < 5U) return UFSD_RC_INVALID;
    mode = *(unsigned *)req->data;
    path = req->data + 4;

    parent_ino   = 0;
    file_name[0] = '\0';
    file_ino = ufsd_path_lookup(disk,
        (path[0] == '/') ? UFSD_ROOT_INO : ufs->cwd_ino,
        path, &parent_ino, file_name);

    new_ino = 0;
    rc      = UFSD_RC_OK;

    if (mode & UFSD_OPEN_WRITE) {
        if (file_ino != 0) {
            /* File exists: truncate */
            if (ufsd_ino_read(disk, file_ino, &dino) != UFSD_RC_OK)
                return UFSD_RC_IO;
            if ((dino.mode & UFSD_IFMT) == UFSD_IFDIR) return UFSD_RC_ISDIR;
            for (i = 0; i < UFSD_NADDR_DIRECT; i++) {
                if (dino.addr[i] != 0) {
                    ufsd_sb_free_block(disk, dino.addr[i]);
                    dino.addr[i] = 0;
                }
            }
            dino.filesize = 0;
            if (ufsd_ino_write(disk, file_ino, &dino) != UFSD_RC_OK)
                return UFSD_RC_IO;
            if (ufsd_sb_write(disk) != UFSD_RC_OK) return UFSD_RC_IO;
            new_ino = file_ino;
        } else {
            /* Create new file */
            if (parent_ino == 0 || file_name[0] == '\0')
                return UFSD_RC_NOFILE;
            if (ufsd_sb_alloc_inode(disk, &new_ino) != UFSD_RC_OK)
                return UFSD_RC_NOINODES;
            memset(&dino, 0, sizeof(dino));
            dino.mode  = (unsigned short)(UFSD_IFREG | 0644U);
            dino.nlink = 1;
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
    gfile->disk_idx  = ufs->disk_idx;
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
    UFSD_UFS    *ufs;
    UFSD_DISK   *disk;
    UFSD_GFILE  *gfile;
    UFSD_DINODE  dino;
    char        *blk;
    char        *dst;
    int          fd;
    unsigned     count;
    unsigned     gidx;
    unsigned     blk_idx;
    unsigned     blk_off;
    unsigned     chunk;
    unsigned     bytes_read;
    int          rc;

    ufs = (UFSD_UFS *)sess->ufs;
    if (!ufs) return UFSD_RC_BADSESS;
    disk = stc->disks[ufs->disk_idx];
    if (!disk) return UFSD_RC_IO;

    if (req->data_len < 8U) return UFSD_RC_INVALID;
    fd    = (int)*(unsigned *)req->data;
    count = *(unsigned *)(req->data + 4);

    if (fd < 0 || fd >= UFSD_MAX_FD) return UFSD_RC_BADFD;
    if (sess->fd_table[fd].gfile_idx == UFSD_FD_UNUSED) return UFSD_RC_BADFD;

    /* Clamp count: 4 bytes for bytes_read + data */
    if (count > UFSREQ_MAX_INLINE - 4U)
        count = UFSREQ_MAX_INLINE - 4U;

    gidx  = sess->fd_table[fd].gfile_idx;
    gfile = &anchor->gfiles[gidx];
    if (!(gfile->flags & UFSD_GF_USED)) return UFSD_RC_BADFD;
    if (!(gfile->open_mode & UFSD_OPEN_READ)) return UFSD_RC_INVALID;

    rc = ufsd_ino_read(disk, gfile->ino, &dino);
    if (rc != UFSD_RC_OK) return rc;

    blk = (char *)malloc(disk->blksize);
    if (!blk) return UFSD_RC_IO;

    bytes_read = 0;
    rc         = UFSD_RC_OK;
    dst        = resp_data + 4; /* first 4 bytes for bytes_read */

    while (bytes_read < count && gfile->position < dino.filesize) {
        blk_idx = gfile->position / (unsigned)disk->blksize;
        blk_off = gfile->position % (unsigned)disk->blksize;

        if (blk_idx >= UFSD_NADDR_DIRECT || dino.addr[blk_idx] == 0) break;

        if (ufsd_blk_read(disk, dino.addr[blk_idx], blk) != UFSD_RC_OK) {
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
    *resp_data_len         = 4U + bytes_read;
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
    UFSD_UFS       *ufs;
    UFSD_DISK      *disk;
    UFSD_GFILE     *gfile;
    UFSD_DINODE     dino;
    char           *blk;
    const char     *src;
    unsigned        mode;
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

    (void)mode;

    ufs = (UFSD_UFS *)sess->ufs;
    if (!ufs) return UFSD_RC_BADSESS;
    disk = stc->disks[ufs->disk_idx];
    if (!disk) return UFSD_RC_IO;

    if (req->data_len < 8U) return UFSD_RC_INVALID;
    fd    = (int)*(unsigned *)req->data;
    count = *(unsigned *)(req->data + 4);
    src   = req->data + 8;

    if (fd < 0 || fd >= UFSD_MAX_FD) return UFSD_RC_BADFD;
    if (sess->fd_table[fd].gfile_idx == UFSD_FD_UNUSED) return UFSD_RC_BADFD;

    if (count == 0) {
        *(unsigned *)resp_data = 0;
        *resp_data_len         = 4U;
        return UFSD_RC_OK;
    }
    if (req->data_len < 8U + count) return UFSD_RC_INVALID;

    gidx  = sess->fd_table[fd].gfile_idx;
    gfile = &anchor->gfiles[gidx];
    if (!(gfile->flags & UFSD_GF_USED)) return UFSD_RC_BADFD;
    if (!(gfile->open_mode & UFSD_OPEN_WRITE)) return UFSD_RC_INVALID;

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

        if (blk_idx >= UFSD_NADDR_DIRECT) {
            rc = UFSD_RC_NOSPACE;
            break;
        }

        if (dino.addr[blk_idx] == 0) {
            /* Allocate new data block */
            if (ufsd_sb_alloc_block(disk, &sector) != UFSD_RC_OK) {
                rc = UFSD_RC_NOSPACE;
                break;
            }
            memset(blk, 0, disk->blksize);
            dino.addr[blk_idx] = sector;
            sb_dirty = 1;
        } else {
            sector = dino.addr[blk_idx];
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

    /* Update inode filesize if position advanced beyond EOF */
    if (bytes_written > 0) {
        if (gfile->position > dino.filesize)
            dino.filesize = gfile->position;
        ufsd_ino_write(disk, gfile->ino, &dino);
        if (sb_dirty)
            ufsd_sb_write(disk);
    }

    *(unsigned *)resp_data = bytes_written;
    *resp_data_len         = 4U;
    return rc;
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

    default:
        return UFSD_RC_NOTIMPL;
    }
}
