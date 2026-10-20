#import "../bookmaster/bookmaster.typ": *

= Creating Disks <ug-disks>

#idx("disk")
#idx("UFSFMT")
Every file system of UFSD lives in one BDAM data set. UFSD does not
allocate or format a data set, so each one must exist and be formatted
before it is named in the configuration. The utility #cmd("UFSFMT") formats
a data set that already exists. It runs as a batch job on MVS, needs nothing
to be uploaded, and writes an empty file system with a root directory.

The sample library has a complete job, #cmd("UFSD.SAMPLIB(UFSFMT)"). It
allocates the data set and formats it in two steps. This chapter explains
that job. The statements and messages of #cmd("UFSFMT") are in the
_UFSD Reference_.

== Planning the Space <ug-disks-space>

#idx("space planning")
#idx("inode")
A file system is a number of blocks. The block size is 4096 bytes unless you
choose another one. The blocks are used like this:

- block 0 is the boot block, which makes the data set a file system;
- block 1 is the superblock;
- the next blocks are the inode list: one inode of 128 bytes for every file
  and every directory;
- all the rest are data blocks, and one of them holds the root directory.

The number of inodes is fixed when the disk is formatted. It cannot grow
afterwards. It follows from the statement #cmd("INODES"), which is not a
percentage of the disk: it is a percentage of the blocks that would be
needed to index everything. At the block size 4096 the default of 10.0
gives about one inode for every 10 blocks, so one file for every 40 KB. The inode list is never shorter than two blocks.
Therefore at the defaults a disk of up to 799 blocks (3.1 MB at the block
size 4096) holds at most 62 files, however much space is free. A directory takes an
inode, too.

This is the planning table from the job, for the block size 4096 and
#cmd("INODES 10.0"):

#tab(caption: [Space planning at block size 4096 and INODES 10.0])[
  #table(columns: (auto, auto, auto, auto, auto, 1fr),
    [Blocks], [Raw size], [Inode blocks], [Files], [Usable], [Use],
    [64], [0.25 MB], [2], [62], [0.23 MB], [],
    [128], [0.50 MB], [2], [62], [0.48 MB], [],
    [256], [1.00 MB], [2], [62], [0.98 MB], [Root disk],
    [512], [2.00 MB], [2], [62], [1.98 MB], [],
    [1280], [5.00 MB], [4], [126], [4.97 MB], [User home],
    [2560], [10.00 MB], [8], [254], [9.96 MB], [Web content],
    [5120], [20.00 MB], [16], [510], [19.93 MB], [],
    [12800], [50.00 MB], [40], [1278], [49.83 MB], [],
  )
]

A root disk holds only the directories that serve as mount points, so 256
blocks are enough. The largest file is 4.06 MB (@ug-intro-limits). UFSFMT
refuses an extent of 1048576 blocks or more (4 GB at the block size 4096).

== Allocating the Data Set <ug-disks-alloc>

#idx("allocation")
The first step of the job allocates the data set:

#screen(```
//ALLOC    EXEC PGM=IEFBR14
//DISKFILE DD  DSN=IBMUSER.UFSHOME,DISP=(NEW,CATLG),
//             UNIT=SYSDA,SPACE=(4096,1280),
//             DCB=(DSORG=PS,RECFM=U,BLKSIZE=4096)
```)

#tab(caption: [The allocation of a UFS data set])[
  #table(columns: (1.2in, 1.2in, 1fr),
    [Parameter], [Value], [Notes],
    [#cmd("DSORG")], [#cmd("PS")], [Physical sequential. UFSD reads and writes
      it with BDAM.],
    [#cmd("RECFM")], [#cmd("U")], [Undefined record format.],
    [#cmd("BLKSIZE")], [#cmd("4096")], [Must be the same as the #cmd("BLKSIZE")
      statement of the format step. UFSD takes the block size of the data set from
      the data set label when it mounts the disk, not from the disk itself. If
      the two differ, the mount fails with #cmd("UFSD062E") and #cmd("UFSD124E").],
    [Space], [blocks], [The primary quantity only.],
  )
]

*Give a primary quantity and no secondary one.* #cmd("UFSFMT") sizes the file
system by writing zero blocks into the primary extent until it is full, and
that count is the size of the file system. Blocks in a secondary extent would
never be formatted and never be reachable.

MVS rounds an allocation up to a track boundary, so the disk usually ends
up a little larger than asked. #cmd("UFSFMT") formats what the primary
extent holds and reports the true count in #cmd("UFSFMT26I"). At the block
size 4096 a track holds 4 blocks on a 3350, 10 on a 3380 and 12 on a 3390.

== Formatting <ug-disks-format>

#idx("format")
The second step runs the utility:

#screen(```
//FORMAT   EXEC PGM=UFSFMT
//STEPLIB  DD  DISP=SHR,DSN=UFSD.LINKLIB
//DISKFILE DD  DISP=OLD,DSN=IBMUSER.UFSHOME
//SYSPRINT DD  SYSOUT=*
//SYSTERM  DD  SYSOUT=*
//SYSUDUMP DD  SYSOUT=*
//SYSIN    DD  *
   /* UFS370 filesystem for IBMUSER, mounted at /u/ibmuser */
   BLKSIZE  4096
   INODES   10.0
   OWNER    IBMUSER
   GROUP    USER
/*
```)

The data set must be allocated with #cmd("DISP=OLD"). #cmd("UFSFMT") refuses a
#cmd("DISP=SHR") allocation with #cmd("UFSFMT25E"), rather than wipe a data
set that another job may be reading. UFSD itself mounts a #cmd("DISP=SHR")
disk read only.

The control statements are read from #cmd("SYSIN") and from nowhere else.
#cmd("PARM=") is not supported and gives #cmd("UFSFMT09W"). A keyword may appear
once. A repeated keyword is an error (#cmd("UFSFMT12E")) and no silent
override. #cmd("SYSIN") may be omitted or be #cmd("DUMMY"); then every default
applies, and the report shows what they were. Comments are written between
#cmd("/*") and #cmd("*/"). They must not start in column 1 of an instream
#cmd("SYSIN"), because that ends the input. Columns 73 to 80 are ignored.

#tab(caption: [The control statements of UFSFMT])[
  #table(columns: (1in, 1.6in, 0.8in, 1fr),
    [Keyword], [Value], [Default], [Meaning],
    [#cmd("BLKSIZE")], [512 to 8192, a multiple of 512], [4096], [The block size.],
    [#cmd("DDNAME")], [a DD name], [#cmd("DISKFILE")], [The DD statement of the data set to format.],
    [#cmd("INODES")], [1.0 to 50.0], [10.0], [Percent of the blocks for inodes.],
    [#cmd("OWNER")], [1 to 8 characters], [none], [The owner of the root directory.],
    [#cmd("GROUP")], [1 to 8 characters], [none], [The group of the root directory.],
    [#cmd("FORCE")], [], [off], [Overwrite an existing file system.],
    [#cmd("QUIET")], [], [off], [Suppress the messages and the report.],
    [#cmd("VERBOSE")], [], [off], [Extra messages for each phase.],
    [#cmd("HELP")], [], [], [Print the help text and stop.],
  )
]

The return code is 0 when the data set was formatted, 4 when #cmd("HELP") was
asked for (nothing was written), and 8 when the format did not complete.

=== Owner and Group <ug-disks-owner>

#idx("owner")
Without #cmd("OWNER") and #cmd("GROUP") the root directory has no owner. The
owner and the group are information stored with the directory. They do not
decide who may write to the file system. That is decided by the
#cmd("OWNER()") of the #cmd("MOUNT") statement alone (@ug-config-access).
Give the #cmd("OWNER") when the disk should carry the name of the user ID it
is handed to. By convention it is the same one that the #cmd("MOUNT")
statement names.

== Reformat Protection <ug-disks-protect>

#idx("FORCE")
#cmd("UFSFMT") reads block 0 before it writes anything. If the data set
already holds a UFS file system, the job stops with return code 8 and
describes what it found. These lines go to #cmd("SYSTERM"):

#screen(raw(read("../ex/ug-disks/format-refused.txt")))

Add #cmd("FORCE") to the control statements to overwrite it. There is no undo:
the first phase of the format zeroes the whole extent. A forced format
still describes what it replaced, so the job log records it:

#screen(raw(read("../ex/ug-disks/format-force.txt")))

The check reads the disk and not the catalog. Therefore a data set that you
have just allocated can trigger it, too: deleting a data set frees its tracks
without erasing them, and a new data set on the same tracks still has the
old boot block under it. #cmd("UFSFMT05E") on a new data set means that the
space was used before and nothing is wrong. Specify #cmd("FORCE"). The check
is best effort: on a data set that has never been written, reading block 0
fails and the format goes on, which is the case with nothing to protect.
The check is also skipped when the block size of the data set is outside the
range 512 to 8192, for instance 0 or 32760: then an existing file
system is overwritten without #cmd("FORCE"). Give the same #cmd("BLKSIZE") in
the allocation as in the format step.

== The Report <ug-disks-report>

#idx("report")
The report goes to #cmd("SYSPRINT"). It ends with the #cmd("MOUNT") statement
for the configuration. The example is the report of a data set that was
allocated with 256 blocks of 4096 bytes and formatted for the owner HERC01 and the
group ADMIN:

#screen(raw(read("../ex/ug-disks/format-report.txt")))

The suggested #cmd("MOUNT") line carries the owner the disk was formatted
for. Without #cmd("OWNER") the line has no #cmd("OWNER()") keyword. An empty
#cmd("OWNER()") would be a syntax error to the parser, and a mount without
#cmd("OWNER") means that any user can write to it.

The job log of the format step contains the system message
#cmd("IEC031I D37-04") with the name of the data set, although the step ends
with the return code 0. It is expected. #cmd("UFSFMT") finds the size of the
data set by writing until the extent is full, and the system reports the end of
the extent (@ug-disks-alloc). Do not mistake it for an error.

The messages of the utility go to two places. The report and the progress
messages go to #cmd("SYSPRINT"). Warnings and errors go to #cmd("SYSTERM"),
and #cmd("QUIET") never suppresses them. Mind the case: the messages of
#cmd("SYSTERM") are in upper case, except the lines that describe an existing
file system (#cmd("UFSFMT06I") and #cmd("UFSFMT07I")), and the messages of
#cmd("SYSPRINT") are in mixed case. The whole list is in the appendix of the
_UFSD Reference_.

== Disks Made on a Host <ug-disks-host>

If a disk should arrive with files on it, the tool #cmd("ufsd-utils") (a separate project,
mvslovers/ufsd-utils) builds an image on a host, fills it with files and
uploads it as a data set. The result is the same format as that of
#cmd("UFSFMT"). The workflow is described in the documentation of
#cmd("ufsd-utils") and not in this book.

== Next Step <ug-disks-next>

When the data sets exist, name them in the configuration (@ug-config).
