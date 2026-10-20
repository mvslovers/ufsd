# Creating UFSD Disks

Each file system that UFSD (ufsd) serves is one data set on MVS. It must be
allocated and formatted before a `ROOT` or `MOUNT` statement names it. This
page only says where to look; the manuals are kept next to the code in
[`docs/books`](books).

| You want to know | Read |
|---|---|
| How to plan the space, allocate the data set and format it with `UFSFMT` (job `UFSD.SAMPLIB(UFSFMT)`), the owner and group, the protection against a reformat, the report | _UFSD User's Guide_ (ML05-0001), chapter 3, "Creating Disks" |
| Every control statement of `UFSFMT`, the phases, the geometry rules, the return codes | _UFSD Reference_ (ML05-0002), chapter 3, "The UFSFMT Utility" |
| The layout of the boot block, the superblock, the inode and the directory entry, the limits of a file system | _UFSD Reference_, chapter 5, "Limits and Disk Format" |
| The meaning of a `UFSFMT` message | _UFSD Reference_, appendix A, "Messages" |

A disk that should arrive with files on it can also be built on a host with
[ufsd-utils](https://github.com/mvslovers/ufsd-utils). That tool has its own
documentation, which is the place to check what it can do.

The books are attached to each release from 1.6.0 on as `ml05-0001-<edition>.pdf`
and `ml05-0002-<edition>.pdf`. The sources are in [`docs/books`](books); the
chapters are in `guide/` and `ref/`.
