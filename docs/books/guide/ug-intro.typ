#import "../bookmaster/bookmaster.typ": *

= Introducing UFSD <ug-intro>

#idx("UFSD", "purpose")
UFSD (ufsd) is a started task for MVS 3.8j that keeps Unix-like file systems
and hands them to other programs. A program in another address space asks
UFSD to open a file, read it, write it, list a directory or create one, and
names the file by a path such as #cmd("/www/index.html"). UFSD is the one
place that reads and writes the disks. The programs that use it today are the
HTTP server HTTPD (httpd) and the FTP server FTPD (ftpd), and any program
that is linked with the client library #cmd("libufs") can do the same
(@ug-libufs).

== File Systems and Mounts <ug-intro-fs>

#idx("file system")
#idx("mount")
Each file system is one BDAM data set that holds a formatted image: a boot
block, a superblock, a list of inodes and the data blocks. The data set is
created and formatted by you before UFSD starts (@ug-disks). UFSD never
allocates or formats a disk by itself.

All file systems form one tree. The first one is the _root_ and is mounted on
#cmd("/"). Every other one is mounted on a directory of a file system that is
already in the tree:

#screen(raw("/                  UFSD.ROOT          (root)
/www               HTTPD.WEBROOT.UFS  read only
/u/ibmuser         IBMUSER.UFSHOME    read/write, owner IBMUSER
/tmp               UFSD.SCRATCH       read/write"))

The configuration member #cmd("UFSDPRM0") lists the data sets and the places
where they are mounted (@ug-config). The operator can add and remove a mount
while UFSD runs (@ug-ops-mount).

== How UFSD Works <ug-intro-how>

#idx("subsystem", "UFS1")
#idx("SSI router")
UFSD is a subsystem, with the subsystem name #cmd("UFS1"), and a started task
with the job name #cmd("UFSD"). The two names differ on purpose: MVS starts
a task over a registered subsystem of the same name in a different way, and
that way does not find the procedure in the usual procedure library.

At the start UFSD does the following:

+ It gets a block of common storage (CSA) for its control blocks, its
  request blocks, its buffers and its trace table.
+ It registers the subsystem #cmd("UFS1") and loads a small router,
  #cmd("UFSDSSIR"), into the common storage.
+ It opens the data sets that the configuration names and mounts them.

A client program calls the router through the subsystem interface, the SVC
34. The router puts the request on a queue in the common storage and wakes
UFSD. UFSD takes the requests from the queue one at a time, does the work on
the disk, and posts the client. The client stays in its own address space
and runs under its own authority.

UFSD handles one request at a time. A slow disk access delays the requests
of all clients behind it. This suits a web server with static content and a
FTP server with a few users. It does not suit a heavy parallel load.

== What UFSD Does Not Do <ug-intro-limits>

#idx("limits")
#idx("access control")
- *UFSD checks the mount, not the file.* A file system is mounted read only
  or read/write, and a read/write mount can name one user ID that may write
  to it. The mode bits and the owner that UFSD stores with each file are
  information only: nothing reads them to allow or refuse an access. Who may
  read is not checked at all. A user who can reach UFSD through a client
  program can read every file on every mounted file system. If that is too
  much, do not mount the file system, or put a server in front that asks
  first (@ug-config-access).
- *Two writers of one file are not serialised.* The programs that use the
  same file must agree among themselves who writes when.
- *A file has at most 4.06 MB.* A file holds 16 direct blocks and one
  indirect block. At the block size 4096 these are 16 blocks of 4096 bytes
  and 1024 blocks of 4096 bytes. Double indirect blocks do not exist.
- *A file system has a fixed number of inodes.* It is set when the disk is
  formatted (@ug-disks-space). A directory takes an inode, too.
- *The sizes of the tables are fixed* when UFSD is built:

#tab(caption: [Fixed sizes of UFSD 1.5.2])[
  #table(columns: (1fr, auto),
    [What], [Number],
    [Mounted file systems, the root included], [16],
    [Mount statements in the configuration], [16],
    [Client sessions at the same time], [64],
    [Open files in one session], [64],
    [Open files in all sessions together], [256],
    [Request blocks in the common storage], [32],
    [Data buffers of 4096 bytes in the common storage], [16],
    [Entries in the trace table], [256],
    [Longest name in a directory], [59 characters],
  )
]

Because the root counts as one of the 16 file systems, at most 15 further
file systems can be mounted. A sixteenth mount statement is read, but the
mount fails with #cmd("UFSD061E MOUNT: DISK TABLE FULL (16 SLOTS)").

== What Is in the Package <ug-intro-package>

#idx("package")
UFSD comes as a release on GitHub, in the repository mvslovers/ufsd. A
release has four files. Only the first is needed for an installation.

#tab(caption: [The files of the release 1.5.2])[
  #table(columns: (2.4in, 1fr),
    [File], [Contents],
    [#cmd("ufsd-1.5.2-dist.zip") or #cmd("ufsd-1.5.2-dist.tar.gz")], [The
      installation package: the two XMIT files, the two JCL jobs and a
      README with the same text as @ug-install.],
    [#cmd("ufsd-1.5.2-load.xmit")], [The load modules alone, as an XMIT
      file. It is also in the installation package.],
    [#cmd("ufsd-1.5.2-lib.tar.gz")], [The headers and #cmd("libufs.a") for
      programs that use UFSD. It is not needed to install or run UFSD.],
  )
]

The installation package holds these files (@ug-install-files). Four load
modules come from them:

#deflist(width: 1.2in,
  [#cmd("UFSD")], [The server, the started task.],
  [#cmd("UFSDSSIR")], [The router that UFSD loads into the common storage.],
  [#cmd("UFSDCLNP")], [The cleanup utility for the common storage after a
    failure (@ug-recover).],
  [#cmd("UFSFMT")], [The utility that formats a data set as a file system
    (@ug-disks).],
)

== How to Read This Book <ug-intro-read>

A new installation follows the chapters in the order of the book:
@ug-install installs the product, @ug-disks creates the disks, @ug-config
describes them to UFSD, and @ug-ops starts it. @ug-recover and @ug-remove are
for the day something goes wrong and the day UFSD leaves the system. Every
command, parameter and message, with all its details, is in the _UFSD
Reference_.

The screens with console text and job output are real. They were taken from
UFSD running on TK5 in a test system, and only the time stamp and the job
name that the console puts before each message are left out. The screens of
the installation, the start, the recovery, the formatting and the removal come
from UFSD 1.5.2; the others were taken from UFSD 1.5.0, and the chapter on
libufs says which versions it used. The examples use
the user ID HERC01 and the data sets of that test system. Replace them with your
own names.
