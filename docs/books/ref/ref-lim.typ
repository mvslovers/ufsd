#import "../bookmaster/bookmaster.typ": *

= Limits and Disk Format <ref-lim>

#idx("limits")
This chapter collects the fixed limits of UFSD (ufsd) 1.5.0, describes the
layout of a disk as far as an operator needs it, and lists the return codes
that UFSD gives to its clients. Where a limit is checked, the message is named.

== Limits <ref-lim-limits>

#tab(caption: [The limits of UFSD 1.5.0])[
  #table(columns: (2.3in, 0.9in, 1fr),
    [Item], [Limit], [Notes],
    [File systems mounted], [16], [Including the root. At most 15 further
      file systems fit. The 16th #cmd("MOUNT") statement of the member is read, but
      the mount gets #cmd("UFSD061E MOUNT: DISK TABLE FULL (16 SLOTS)") and
      #cmd("UFSD123W"). The same limit holds for the console command.],
    [#cmd("MOUNT") statements in the member], [16], [The 17th and every later one gets
      #cmd("UFSD101W") and is skipped.],
    [Sessions], [64], [One session for each client program.],
    [Open files in a session], [64], [The count is shown as #cmd("FDS=n/64") by
      #cmd("SESSIONS").],
    [Open files in all sessions], [256], [The table of open files.],
    [Request blocks], [32], [In the common storage. #cmd("STATS") shows how many
      are free.],
    [Buffers of 4096 bytes], [16], [In the common storage. They carry the data of
      reads and writes.],
    [Entries of the trace table], [256], [The oldest entry is overwritten.],
    [Queued console requests], [5], [The start request and four #cmd("MODIFY")
      commands.],
    [Length of a #cmd("MODIFY") command], [63], [Longer text is cut.],
    [Length of a line of the member], [511], [Longer lines are split, and the rest
      is read as a new line.],
    [Length of a value in parentheses], [255], [],
    [Length of #cmd("DSN()")], [44], [Longer values are cut.],
    [Length of #cmd("PATH()")], [127], [Longer values are cut.],
    [Length of #cmd("OWNER()")], [8], [Longer values are cut.],
    [Length of a file name], [59], [A longer name is not found, and a client gets the return code 28 (NOFILE), also when it wants to create the file.],
    [Length of a path in a request], [255], [The client library sends a path of up
      to 255 characters whole and refuses a longer one before it sends any
      request. #cmd("ufs_chgdir"), #cmd("ufs_mkdir"), #cmd("ufs_rmdir"),
      #cmd("ufs_remove") and #cmd("ufs_stat") return 20 (INVALID) for a path of 256
      characters or more\; the first four also set #cmd("last_rc"). #cmd("ufs_diropen")
      returns NULL. #cmd("ufs_fopen") takes 251 characters and returns NULL for a
      longer path. UFSD 1.5.0 cut a path of 256 characters or more to 255
      in #cmd("ufs_chgdir"), #cmd("ufs_mkdir"), #cmd("ufs_rmdir") and
      #cmd("ufs_remove") and sent it with its 256th character in place of the
      terminating null\; UFSD 1.6.0 refuses it. Keep paths well below 250
      characters.],
    [Entries in a directory], [_b_/64 × 16], [A directory has direct blocks
      only: 16 blocks of _b_/64 entries, the entries "." and ".." included.
      At 4096 this is 1024 entries. More gets the return code 44 (no space),
      also when the disk is empty.],
    [Inodes in a file system], [fixed at format], [Set by #cmd("INODES") of
      #cmd("UFSFMT"). A directory takes an inode, too.],
    [Blocks in a file system], [not fixed], [#cmd("UFSFMT") refuses an extent of 1048576 blocks or more
      (#cmd("UFSFMT24E")), a safeguard of the program.],
    [Block size], [512 to 8192], [A multiple of 512. The default is 4096. UFSD takes the block size
      from the data set label at the mount; a data set whose label differs from
      the block size of the disk fails the mount with #cmd("UFSD062E") and
      #cmd("UFSD124E").],
    [Size of a file], [(16 + _b_/4) × _b_], [_b_ is the block size. At 4096 this
      is 4.06 MB. See @ref-lim-file.],
  )
]

The values in parentheses, the lengths of #cmd("DSN()"), #cmd("PATH()") and
#cmd("OWNER()"), and the other rules of the member are in @ref-cfg.

=== The Work of the Server <ref-lim-work>

UFSD handles one request at a time. The requests of all clients are queued
and served in turn, so a slow disk delays every client. UFSD does not lock
files. Each request is served whole, but two clients that write the same file
are not kept apart.

The pools in the common storage are fixed. If all 32 request blocks are in
use, a client gets the return code 4 and has to try again. The same code
answers a request for which a session, an open file or a file descriptor is
needed and the table is full.

=== The Size of a File <ref-lim-file>

#idx("file size")
A file is held in 16 direct blocks and one single indirect block. The indirect
block is a block of block numbers, four bytes each, so it points to _b_/4
data blocks. The largest file therefore has (16 + _b_/4) blocks, and a write
beyond that gets the return code 44 (no space), also when the disk is not
full. There are no double or triple indirect blocks.

#tab(caption: [The largest file for each block size])[
  #table(columns: (1in, 1fr),
    [Block size], [Largest file],
    [512], [73 728 bytes (72 KB)],
    [1024], [278 528 bytes (272 KB)],
    [2048], [1 081 344 bytes (1.03 MB)],
    [4096], [4 259 840 bytes (4.06 MB)],
    [8192], [16 908 288 bytes (16.1 MB)],
  )
]

== Free Space <ref-lim-free>

#idx("free space")
Two things can run out on a file system: the data blocks and the inodes. A
write that finds no free block gets the return code 44. The creation of a file
or a directory that finds no free inode gets the return code 48. Both counts
are in the superblock of the disk, and #cmd("STATS") shows them with
#cmd("UFSD018I") (@ref-cmd-stats). The number of inodes is fixed at the format
of the disk, and so is the number of blocks. Only a larger disk helps, and a
larger disk is a new file system that you fill with the files of the old one.

The superblock holds a cache of up to 51 free block numbers and up to 64 free
inode numbers. The two numbers after #cmd("FREEBLK=") in #cmd("UFSD018I") are
the number of entries in the cache and the total number of free blocks. Read
the free space from the second number.

#note[*A defect* (ufsd\#130): inodes of removed files and directories can be
lost for good. A removed inode is not cleared on the disk, and once more than
64 inodes have been freed since the start, the cache cannot hold the others.
#cmd("F UFSD,REBUILD") looks for inodes whose mode is 0, so it does not find
them, and #cmd("STATS") still shows the old count of free inodes. On a file
system with 126 free inodes, creating 125 files in one directory and removing
them again left no free inode: every later create got the return code 48. Only
a new format with #cmd("FORCE") (UFSD stopped) gave the inodes back.]

== The Layout of a Disk <ref-lim-format>

#idx("disk format")
The format of a disk is that of the earlier product ufs370, and the tool
ufsd-utils writes the same format. The layout is:

#tab(caption: [The blocks of a UFSD disk])[
  #table(columns: (1.4in, 1fr),
    [Block], [Content],
    [0], [The boot block. It makes the data set a file system, and it is written
      last by #cmd("UFSFMT").],
    [1], [The superblock, 512 bytes at the start of the block.],
    [2 to the first data block − 1], [The inode list: 128 bytes for each inode.
      At least two blocks.],
    [The data blocks], [The rest. They hold the directories, the files, the
      indirect blocks and the chain blocks of the free list.],
  )
]

The inode with the number 1 is reserved, and the root directory is the inode
with the number 2. A UFSD disk is a BDAM data set with fixed-size blocks, and
the numbers above are block numbers (sectors) of that data set, counted from 0.
The block that holds the root directory is not always the first data block:
see @ref-fmt-geometry.

=== The Boot Block

#tab(caption: [The boot block])[
  #table(columns: (1in, 0.9in, 1fr),
    [Offset], [Length], [Content],
    [0], [2], [The type of the disk, 2 for a UFS disk.],
    [2], [2], [The complement of the type (hex FFFD). A disk whose type and
      complement do not add up to FFFF is refused with #cmd("UFSD044E").],
    [4], [2], [The block size.],
    [8], [8], [The time of the creation.],
    [16], [8], [The time of the last update.],
    [24], [1], [The version of the extension, 1.],
  )
]

=== The Superblock

#tab(caption: [The superblock, with the offsets in hexadecimal])[
  #table(columns: (0.8in, 0.8in, 1fr),
    [Offset], [Length], [Content],
    [000], [4], [The first data block.],
    [004], [4], [The number of blocks of the disk.],
    [008], [1], [The lock byte of the free blocks. #cmd("UFSFMT") writes 0;
      UFSD 1.5.0 neither sets nor reads it.],
    [009], [1], [The lock byte of the free inodes. The same.],
    [00A], [1], [The flag: modified.],
    [00B], [1], [The flag: read only.],
    [00C], [4], [The time of the last update. #cmd("UFSFMT") writes 0;
      UFSD 1.5.0 neither sets nor reads it.],
    [010], [4], [The total number of free blocks.],
    [014], [4], [The total number of free inodes.],
    [018], [4], [The time of the creation. The same as 00C.],
    [01C], [4], [The number of entries in the cache of free blocks.],
    [020], [204], [The cache of free blocks, 51 entries.],
    [0EC], [4], [The number of entries in the cache of free inodes.],
    [0F0], [256], [The cache of free inodes, 64 entries.],
    [1F0], [4], [The number of inodes in a block.],
    [1F4], [4], [The block size as a shift count (12 for 4096).],
    [1F8], [4], [The block where the inode list begins.],
    [1FC], [4], [The block where the last scan for free blocks stopped. UFSD
      sets it during the scan and writes it with the rest of the superblock,
      so the value reaches the disk; UFSD 1.5.0 never reads it, and
      #cmd("UFSFMT") writes 0.],
  )
]

The times of the superblock are not set by #cmd("UFSFMT"); it keeps the times
in the boot block.

=== The Inode

#tab(caption: [The inode, 128 bytes])[
  #table(columns: (0.8in, 0.8in, 1fr),
    [Offset], [Length], [Content],
    [000], [2], [The mode: the type (directory or regular file) and the
      permission bits.],
    [002], [2], [The number of links.],
    [004], [4], [The size of the file in bytes.],
    [008], [8], [The time of the creation.],
    [010], [8], [The time of the last modification.],
    [018], [8], [The time of the last access.],
    [020], [9], [The owner, with a terminating zero.],
    [029], [9], [The group, with a terminating zero.],
    [032], [2], [The code page, 0 for the default.],
    [034], [76], [Nineteen block numbers: sixteen direct blocks and the single
      indirect block. The last two are not used.],
  )
]

The mode, the owner and the group are information only. UFSD makes no check
of them for any access. Whether a client may write is decided by the mode of
the mount (read only or read/write) and by the #cmd("OWNER()") of the mount
(@ref-cfg-access).

=== The Directory Entry

A directory is a file of entries of 64 bytes each: a four-byte inode number
(0 for a free entry) and a name of at most 59 characters, followed by a
terminating zero.

== Return Codes of the Requests <ref-lim-rc>

#idx("return codes", "requests")
Every request of a client ends with one of these codes. The client library
passes the code to the program. In the trace (#cmd("TRACE DUMP"),
@ref-cmd-trace) the code is shown in hexadecimal in the field #cmd("RC=").

#tab(caption: [The return codes of the requests])[
  #table(columns: (0.8in, 0.7in, 1.4in, 1fr),
    [Decimal], [Hex], [Name], [Meaning],
    [0], [0000], [OK], [The request succeeded.],
    [4], [0004], [NOREQ], [No free request block, session slot, open file slot or
      file descriptor. Try again later.],
    [8], [0008], [CORRUPT], [The request block is damaged (the eye catcher is wrong).
      #cmd("UFSD150E").],
    [12], [000C], [BADFUNC], [The function code is not valid. #cmd("UFSD151E").],
    [16], [0010], [BADSESS], [The session is not valid.],
    [20], [0014], [INVALID], [An argument failed its check.],
    [24], [0018], [NOTIMPL], [The function is not implemented.],
    [28], [001C], [NOFILE], [The file or the path does not exist.],
    [32], [0020], [EXIST], [The file or the directory exists already.],
    [36], [0024], [NOTDIR], [A part of the path is not a directory.],
    [40], [0028], [ISDIR], [The name is a directory, and a file was expected.],
    [44], [002C], [NOSPACE], [No free block, or the file reached its largest size
      (@ref-lim-file).],
    [48], [0030], [NOINODES], [No free inode.],
    [52], [0034], [IO], [An input/output error.],
    [56], [0038], [BADFD], [The file descriptor is not valid.],
    [60], [003C], [NOTEMPTY], [The directory is not empty.],
    [64], [0040], [NAMETOOLONG], [A component of a mount point path is longer than 59 characters. Only UFSD itself gives this code, when it creates the mount points at the start. A client never gets it.],
    [68], [0044], [ROFS], [The file system is mounted read only.],
    [72], [0048], [EACCES], [The user ID of the session is not the #cmd("OWNER()")
      of the mount (@ref-cfg-access).],
  )
]

The counter #cmd("ERRORS") of #cmd("STATS") counts only the codes that point
to a fault of the system: CORRUPT, BADFUNC, BADSESS, INVALID, NOSPACE, NOINODES
and IO. The others are normal answers.
