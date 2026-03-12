/* UFSD#DIR.C - UFS Directory and Path Operations
**
** AP-1e: Directory entry lookup, add, remove, and path resolution.
** Operates on direct blocks only (addr[0..15]); indirect blocks
** are a deferred AP-1e simplification.
**
** ufsd_dir_lookup (UFSD@DLU)  scan directory for a name, return inode#
** ufsd_dir_add    (UFSD@DAD)  add a name->inode entry to a directory
** ufsd_dir_remove (UFSD@DRM)  remove a name entry from a directory
** ufsd_path_lookup(UFSD@PLU)  resolve a path to an inode number
**
** All functions return 0/UFSD_RC_NOFILE on "not found" and
** positive UFSD_RC_* on error.
*/

#include "ufsd.h"
#include <string.h>
#include <stdlib.h>

/* ============================================================
** ufsd_dir_lookup
**
** Scan directory 'dir_ino' for an entry named 'name'.
** Returns the entry's inode number, or 0 if not found.
** ============================================================ */
unsigned
ufsd_dir_lookup(UFSD_DISK *disk, unsigned dir_ino, const char *name)
{
    UFSD_DINODE  dino;
    UFSD_DIRENT *de;
    char        *buf;
    unsigned     i;
    unsigned     j;
    unsigned     nde;
    unsigned     n_blocks;
    unsigned     result;

    if (!disk || dir_ino == 0 || !name) return 0;

    if (ufsd_ino_read(disk, dir_ino, &dino) != UFSD_RC_OK) return 0;

    nde      = (unsigned)disk->blksize / UFSD_DIRENT_SIZE;
    n_blocks = (dino.filesize + (unsigned)disk->blksize - 1U)
               / (unsigned)disk->blksize;

    buf = (char *)malloc(disk->blksize);
    if (!buf) return 0;

    result = 0;

    for (i = 0; i < UFSD_NADDR_DIRECT && i < n_blocks; i++) {
        if (dino.addr[i] == 0) continue;
        if (ufsd_blk_read(disk, dino.addr[i], buf) != UFSD_RC_OK) continue;

        de = (UFSD_DIRENT *)buf;
        for (j = 0; j < nde; j++, de++) {
            if (de->ino != 0 && strcmp(de->name, name) == 0) {
                result = de->ino;
                goto done;
            }
        }
    }

done:
    free(buf);
    return result;
}

/* ============================================================
** ufsd_dir_add
**
** Add an entry (ino, name) to directory 'dir_ino'.
** First scans existing blocks for a free slot (ino==0).
** If no free slot exists, allocates a new data block (updates
** disk->sb in memory; caller must call ufsd_sb_write()).
**
** Returns UFSD_RC_OK on success, UFSD_RC_* on error.
** ============================================================ */
int
ufsd_dir_add(UFSD_DISK *disk, unsigned dir_ino,
             const char *name, unsigned ino)
{
    UFSD_DINODE  dino;
    UFSD_DIRENT *de;
    char        *buf;
    unsigned     i;
    unsigned     j;
    unsigned     nde;
    unsigned     n_blocks;
    unsigned     new_blk;
    int          rc;

    if (!disk || dir_ino == 0 || !name || ino == 0) return UFSD_RC_INVALID;

    if (ufsd_ino_read(disk, dir_ino, &dino) != UFSD_RC_OK) return UFSD_RC_IO;

    nde      = (unsigned)disk->blksize / UFSD_DIRENT_SIZE;
    n_blocks = (dino.filesize + (unsigned)disk->blksize - 1U)
               / (unsigned)disk->blksize;

    buf = (char *)malloc(disk->blksize);
    if (!buf) return UFSD_RC_IO;

    rc = UFSD_RC_IO;

    /* Search existing blocks for a free slot */
    for (i = 0; i < UFSD_NADDR_DIRECT && i < n_blocks; i++) {
        if (dino.addr[i] == 0) continue;
        if (ufsd_blk_read(disk, dino.addr[i], buf) != UFSD_RC_OK) continue;

        de = (UFSD_DIRENT *)buf;
        for (j = 0; j < nde; j++, de++) {
            if (de->ino == 0) {
                de->ino = ino;
                memset(de->name, 0, sizeof(de->name));
                strncpy(de->name, name, (unsigned)UFSD_NAME_MAX);
                if (ufsd_blk_write(disk, dino.addr[i], buf) == UFSD_RC_OK)
                    rc = UFSD_RC_OK;
                goto done;
            }
        }
    }

    /* No free slot: allocate a new block */
    if (n_blocks >= UFSD_NADDR_DIRECT) {
        rc = UFSD_RC_NOSPACE;
        goto done;
    }

    if (ufsd_sb_alloc_block(disk, &new_blk) != UFSD_RC_OK) {
        rc = UFSD_RC_NOSPACE;
        goto done;
    }

    /* Initialize new block: first entry holds the new name */
    memset(buf, 0, disk->blksize);
    de      = (UFSD_DIRENT *)buf;
    de->ino = ino;
    memset(de->name, 0, sizeof(de->name));
    strncpy(de->name, name, (unsigned)UFSD_NAME_MAX);

    if (ufsd_blk_write(disk, new_blk, buf) != UFSD_RC_OK) {
        ufsd_sb_free_block(disk, new_blk);
        goto done;
    }

    /* Update directory inode: new block address and filesize */
    dino.addr[n_blocks] = new_blk;
    dino.filesize      += (unsigned)disk->blksize;
    if (ufsd_ino_write(disk, dir_ino, &dino) == UFSD_RC_OK)
        rc = UFSD_RC_OK;
    else
        ufsd_sb_free_block(disk, new_blk);

done:
    free(buf);
    return rc;
}

/* ============================================================
** ufsd_dir_remove
**
** Remove the entry named 'name' from directory 'dir_ino'.
** Zeroes the entry (ino=0, name="") and writes the block back.
** Does NOT shrink the directory or compact entries.
**
** Returns UFSD_RC_OK on success, UFSD_RC_NOFILE if not found,
** UFSD_RC_IO on I/O error.
** ============================================================ */
int
ufsd_dir_remove(UFSD_DISK *disk, unsigned dir_ino, const char *name)
{
    UFSD_DINODE  dino;
    UFSD_DIRENT *de;
    char        *buf;
    unsigned     i;
    unsigned     j;
    unsigned     nde;
    unsigned     n_blocks;
    int          rc;

    if (!disk || dir_ino == 0 || !name) return UFSD_RC_INVALID;

    if (ufsd_ino_read(disk, dir_ino, &dino) != UFSD_RC_OK) return UFSD_RC_IO;

    nde      = (unsigned)disk->blksize / UFSD_DIRENT_SIZE;
    n_blocks = (dino.filesize + (unsigned)disk->blksize - 1U)
               / (unsigned)disk->blksize;

    buf = (char *)malloc(disk->blksize);
    if (!buf) return UFSD_RC_IO;

    rc = UFSD_RC_NOFILE;

    for (i = 0; i < UFSD_NADDR_DIRECT && i < n_blocks; i++) {
        if (dino.addr[i] == 0) continue;
        if (ufsd_blk_read(disk, dino.addr[i], buf) != UFSD_RC_OK) continue;

        de = (UFSD_DIRENT *)buf;
        for (j = 0; j < nde; j++, de++) {
            if (de->ino != 0 && strcmp(de->name, name) == 0) {
                de->ino = 0;
                memset(de->name, 0, sizeof(de->name));
                if (ufsd_blk_write(disk, dino.addr[i], buf) == UFSD_RC_OK)
                    rc = UFSD_RC_OK;
                else
                    rc = UFSD_RC_IO;
                goto done;
            }
        }
    }

done:
    free(buf);
    return rc;
}

/* ============================================================
** ufsd_path_lookup
**
** Walk a path from start_ino and return the inode number of the
** final component, or 0 if the path does not exist.
**
** Parameters:
**   disk          - disk to search on
**   start_ino     - starting inode (use UFSD_ROOT_INO for absolute)
**   path          - NUL-terminated path (absolute or relative)
**   out_parent_ino- if not NULL, receives the parent dir inode
**   out_name      - if not NULL, receives the last component name
**                   (buffer must be at least UFSD_NAME_MAX+1 bytes)
**
** For absolute paths beginning with '/', start_ino is overridden
** to UFSD_ROOT_INO.  For "/", returns UFSD_ROOT_INO with
** out_parent_ino=0 and out_name="".
**
** "." and ".." components are not resolved (deferred).
** ============================================================ */
unsigned
ufsd_path_lookup(UFSD_DISK *disk, unsigned start_ino,
                 const char *path,
                 unsigned *out_parent_ino,
                 char *out_name)
{
    char         comp[UFSD_NAME_MAX + 1];
    const char  *p;
    const char  *end;
    unsigned     cur_ino;
    unsigned     par_ino;
    unsigned     found;
    int          n;

    if (!disk || !path) return 0;

    cur_ino = start_ino;
    par_ino = 0;

    if (out_parent_ino) *out_parent_ino = 0;
    if (out_name)       out_name[0]     = '\0';

    p = path;

    /* Absolute path: start from root */
    if (*p == '/') {
        cur_ino = UFSD_ROOT_INO;
        p++;
    }

    /* Path was just "/" or empty after root */
    if (*p == '\0') {
        if (out_parent_ino) *out_parent_ino = 0;
        if (out_name)       out_name[0]     = '\0';
        return cur_ino;
    }

    for (;;) {
        /* Skip consecutive slashes */
        while (*p == '/') p++;
        if (*p == '\0') break;

        /* Extract next component */
        end = p;
        while (*end && *end != '/') end++;
        n = (int)(end - p);

        if (n == 0) break;
        if (n > (int)UFSD_NAME_MAX) {
            /* Name too long: record parent and name prefix, return not-found */
            if (out_parent_ino) *out_parent_ino = cur_ino;
            if (out_name) {
                memcpy(out_name, p, (unsigned)UFSD_NAME_MAX);
                out_name[UFSD_NAME_MAX] = '\0';
            }
            return 0;
        }

        memcpy(comp, p, (unsigned)n);
        comp[n] = '\0';

        par_ino = cur_ino;
        if (out_parent_ino) *out_parent_ino = par_ino;
        if (out_name) {
            memcpy(out_name, comp, (unsigned)n + 1U);
        }

        /* Advance p past this component and optional slash */
        p = end;
        if (*p == '/') p++;

        /* Lookup this component in cur_ino */
        found = ufsd_dir_lookup(disk, cur_ino, comp);

        if (*p == '\0') {
            /* Last component: return whether it exists */
            return found;
        }

        /* Intermediate component: must exist to continue */
        if (found == 0) return 0;

        cur_ino = found;
    }

    /* Reached end via trailing slashes: return current dir */
    if (out_parent_ino) *out_parent_ino = 0;
    return cur_ino;
}
