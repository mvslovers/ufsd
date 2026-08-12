/* UFSDMNT.H - Mount point table (issue #52)
**
** Where each mounted filesystem hangs in the tree, expressed as inode
** numbers instead of paths.  do_dirread (src/ufsd#fil.c) consults this
** once per directory entry to decide whether the entry it just read is
** a mount point and its metadata therefore lives on another disk --
** which is what makes `ls /u` and `ufs_stat /u/user` agree, the way
** Unix `ls -l` crosses a mount.  A per-entry path reconstruction would
** need a reverse ".." walk and a stack buffer for every entry; two
** integer compares need neither.
**
** Deliberately free of MVS and UFSD dependencies: the table is nothing
** but indices and inode numbers here, so the same source compiles for
** cc370 and for a host compiler and the index arithmetic -- the part
** that silently breaks when a filesystem is unmounted and the disk
** array is compacted underneath it -- is testable without an MVS round
** trip (test/mvs/tstufsmp.c, `make test-host`).
**
** Everything that reads a disk (inode lookup at mount time, crossing
** into another disk's root inode) stays in src/ufsd#ini.c and
** src/ufsd#fil.c and is target-only.
*/

#ifndef UFSDMNT_H
#define UFSDMNT_H

/* No parent: the root filesystem, and any entry whose parent has been
** unmounted.  Never a valid index, so a table cleared to zero is not
** silently "everything hangs off disk 0". */
#define UFSD_MNT_NOPARENT  (-1)

/* External names, as everywhere else in UFSD (see include/ufsd.h).
** Not cosmetic here: cc370 derives an 8-character MVS name from the
** first characters of the C name, so ufsd_mount_child and
** ufsd_mount_remove would both become UFSD@MOU and the linker would
** resolve calls to whichever it saw first.  The host build keeps the
** C names -- nothing there is limited to 8 characters. */
#ifdef __MVS__
#define UFSD_MNT_EXTNAME(n)  asm(n)
#else
#define UFSD_MNT_EXTNAME(n)
#endif

/* ============================================================
** UFSD_MOUNTPT
**
** One entry per disk, parallel to UFSD_STC.disks[] and indexed the
** same way.  Filled when the disk is mounted, because the mount point
** directory can only be looked up while its own parent is reachable,
** and read once per directory entry afterwards.
**
**   pdisk  index of the disk holding the mount point directory
**   ino    inode of the mount point directory itself, on pdisk
**          (/u/user -- the directory the filesystem covers)
**   pino   inode of the directory containing it, on pdisk
**          (/u -- what ".." resolves to at the mounted root)
**
** ino and pino are on the SAME disk.  pino is what the mounted
** filesystem's ".." must answer with: in Unix "/u/user/.." is "/u",
** not the covered directory.
** ============================================================ */
typedef struct ufsd_mountpt UFSD_MOUNTPT;
struct ufsd_mountpt {
    int      pdisk;
    unsigned ino;
    unsigned pino;
};

/* ============================================================
** ufsd_mount_child
**
** Which filesystem is mounted on directory `ino` of disk `pdisk`?
**
**   tab    &stc->mountpt[0]
**   ntab   stc->ndisks
**
** Returns the disk index of the filesystem mounted there, or -1 when
** the directory carries no mount.
**
** Both halves of the key are compared: inode numbers are per disk, so
** inode 5 of the root disk and inode 5 of a mounted disk are different
** directories, and matching on the inode alone would cross a mount
** that is not there.  A zero inode is no directory at all (a free
** directory slot) and never matches.
** ============================================================ */
int ufsd_mount_child(const UFSD_MOUNTPT *tab, unsigned ntab,
                     int pdisk, unsigned ino)     UFSD_MNT_EXTNAME("UFSD@MNC");

/* ============================================================
** ufsd_mount_remove
**
** Keep the table valid across an UNMOUNT.  ufsd_disk_umount compacts
** UFSD_STC.disks[] -- every disk above the removed one moves down a
** slot -- so stored parent indices stop pointing at the disk they were
** recorded for unless they move with it.
**
**   tab      &stc->mountpt[0]
**   ntab     stc->ndisks BEFORE the removal
**   removed  index being unmounted
**
** Entries above `removed` shift down; a parent index above `removed`
** is decremented with it.  An entry whose parent WAS the removed disk
** keeps its slot but loses its mount point: its directory went away
** with the filesystem that held it, and a stale inode number there
** would make do_dirread cross into it from whatever disk later takes
** the index.  Such an entry is reset to UFSD_MNT_NOPARENT, which costs
** nothing but the crossing -- the filesystem stays mounted and
** reachable by path.
** ============================================================ */
void ufsd_mount_remove(UFSD_MOUNTPT *tab, unsigned ntab,
                       unsigned removed)          UFSD_MNT_EXTNAME("UFSD@MNR");

#endif /* UFSDMNT_H */
