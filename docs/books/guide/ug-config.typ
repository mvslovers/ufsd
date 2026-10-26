#import "../bookmaster/bookmaster.typ": *

= Configuring UFSD <ug-config>

#idx("configuration")
UFSD reads its configuration from one PARMLIB member when it starts. The
member names the root file system and the other file systems with the places
where they are mounted. This chapter shows the procedure that starts UFSD,
the member, and the rules by which UFSD reads it. Every parameter is listed in
the _UFSD Reference_.

== The Procedure <ug-config-proc>

#idx("procedure")
The procedure #cmd("UFSD") in #cmd("UFSD.SAMPLIB") is the started task. Copy
it into a procedure library of the started tasks (@ug-install-proc):

#screen(```
//UFSD     PROC M=UFSDPRM0,
//            D='SYS2.PARMLIB'
//UFSD     EXEC PGM=UFSD,REGION=4M,TIME=1440
//STEPLIB  DD  DISP=SHR,DSN=UFSD.LINKLIB
//UFSDPRM  DD  DISP=SHR,DSN=&D(&M),FREE=CLOSE
//SYSUDUMP DD  SYSOUT=*
```)

The shipped file carries the name #cmd("@LINKLIB@") in the #cmd("STEPLIB")
statement. The package puts the name of the library there when it is built,
so the copy in #cmd("UFSD.SAMPLIB") already reads #cmd("UFSD.LINKLIB").

#deflist(width: 1.3in,
  [#cmd("STEPLIB")], [The library with the load modules. UFSD, the router and
    the utilities are all loaded from there.],
  [#cmd("UFSDPRM")], [The configuration member. UFSD reads it once, at the
    start, and the procedure frees it afterwards.],
  [#cmd("SYSUDUMP")], [Where a dump goes if UFSD abends.],
  [#cmd("M")], [The member. The default is #cmd("UFSDPRM0").],
  [#cmd("D")], [The PARMLIB data set. The default is #cmd("SYS2.PARMLIB").],
)

The region is 4 MB, and the value 1440 for the time limit means that there is no limit. On
TK5 the data set #cmd("SYS2.PARMLIB") does not exist. Edit the default in your
copy, or override it when you start (@ug-install-parmlib):

#screen(```
S UFSD                              default configuration
S UFSD,M=UFSDPRM1                   another member
S UFSD,D='SYS1.PARMLIB'             another PARMLIB
S UFSD,M=UFSDPRM1,D='MY.PARMLIB'    both
```)

== The Member <ug-config-member>

#idx("UFSDPRM")
#idx("ROOT")
#idx("MOUNT")
The member has two kinds of statements, #cmd("ROOT") and #cmd("MOUNT"):

#screen(```
/* SYS2.PARMLIB(UFSDPRM0) */

ROOT     DSN(UFSD.ROOT)

MOUNT    DSN(HTTPD.WEBROOT.UFS)  PATH(/www)          MODE(RO)
MOUNT    DSN(IBMUSER.UFSHOME)    PATH(/u/ibmuser)    MODE(RW) OWNER(IBMUSER)
MOUNT    DSN(UFSD.SCRATCH)       PATH(/tmp)          MODE(RW)
```)

A statement is on one line. After the name of the statement come the
parameters, each as a keyword followed by a value in parentheses, in any
order. The data sets named must exist and be formatted (@ug-disks).

*ROOT.* There is one #cmd("ROOT") statement, and it is required. It names the
data set of the root file system, mounted on #cmd("/"). One megabyte is
enough, because the root holds only the directories where the other file
systems are mounted. Clients can never write to the root: UFSD makes it
writable only while it creates the mount points, and switches it to read
only before the first client can reach it. A second #cmd("ROOT") statement
replaces the first.

*MOUNT.* A #cmd("MOUNT") statement names a data set, the path where it is
mounted, and optionally the mode and the owner:

#tab(caption: [The parameters of the MOUNT statement])[
  #table(columns: (1.6in, 1fr),
    [Parameter], [Meaning],
    [#cmd("DSN(name)")], [Required. The data set to mount.],
    [#cmd("PATH(/path)")], [Required. The absolute path of the mount point.],
    [#cmd("MODE(RO)") or #cmd("MODE(RW)")], [Read only or read/write. The
      default is read only.],
    [#cmd("OWNER(userid)")], [Only this user ID may write to the file system
      (@ug-config-access).],
  )
]

A #cmd("MOUNT") statement without #cmd("DSN()") gets #cmd("UFSD102W"), and one
without #cmd("PATH()") gets #cmd("UFSD103W"). UFSD skips that statement and
reads on.

=== The Mount Points <ug-config-points>

#idx("mount point")
UFSD creates the directory for a mount point itself, in the file system that
holds it, when it processes the statement at the start. It does so even if
that file system is the read-only root. The new directory belongs to the
owner and group of the directory above it. If the directory cannot be created,
UFSD says #cmd("UFSD122W") and tries the mount anyway.

File systems may be nested. UFSD sorts the statements by the depth of the
path and mounts the shallow ones first, so the order in the member does not
matter. In the example the path #cmd("/u/ibmuser") needs the directory
#cmd("/u"), which UFSD creates in the root.

A file system mounted at a place hides what its parent holds under that
path. Do not mount on a directory that has files in it.

=== How the Data Sets Are Opened <ug-config-open>

UFSD allocates each data set itself, and gives it the ddname #cmd("UFD00001")
and the following. The root is allocated with #cmd("DISP=OLD"). A child is
allocated with #cmd("DISP=OLD") if it is mounted read/write, and with
#cmd("DISP=SHR") if it is mounted read only. This has two results:

- No other job can use a read/write file system while UFSD runs. Another job
  that needs it must wait, or fail with an allocation error.
- A read only file system can be read by another job at the same time.

=== The Number of Mounts <ug-config-number>

UFSD has 16 places for file systems, and the root takes one. So at most 15
child mounts succeed. A member may hold 16 #cmd("MOUNT") statements. A 17th
gets #cmd("UFSD101W") when the member is read. The 16th statement is read, but
its mount fails with #cmd("UFSD061E MOUNT: DISK TABLE FULL (16 SLOTS)") and
#cmd("UFSD123W").

== Access Control <ug-config-access>

#idx("access control")
#idx("OWNER")
UFSD decides on a write in two places, and in this order:

+ *The mode of the mount.* A file system mounted read only refuses every
  write: opening a file for writing, writing, creating or removing a
  directory, and removing a file. The client gets the return code
  #cmd("UFSD_RC_ROFS").
+ *The owner of the mount.* If the #cmd("MOUNT") statement has an #cmd("OWNER"),
  only a session of that user ID may write. The comparison is exact. A
  session of any other user ID, and a session that has none, gets
  #cmd("UFSD_RC_EACCES"). A session starts with no user ID, and it does not
  take the one of the job. The client program names a user ID with
  #cmd("ufs_setuser"), and UFSD does not check the name (@ug-libufs-auth). A mount without #cmd("OWNER")
  accepts a write from any session, if it is read/write.

That is all. Reading is not checked: every session can read every file of
every mounted file system. UFSD has no permission check for single files.
The mode bits, the owner and the group stored with each inode are
information. They appear in the answer to a #cmd("stat") and in a directory
listing, and nothing reads them to decide on an access.

Because of this, the owner stored in the root directory of a disk (the
#cmd("OWNER") of #cmd("UFSFMT")) and the #cmd("OWNER()") of the #cmd("MOUNT")
statement can differ. UFSD points out the difference with #cmd("UFSD125W") when it
mounts, and it follows the #cmd("OWNER()"). To change who may write, change the
#cmd("OWNER()"). Formatting again or changing the owners in the inodes has no
effect on access.

If a file system holds data that not every client may see, do not mount it
for a server that serves everybody, or let the server decide who may read.

== The Rules of the Parser <ug-config-rules>

#idx("parser")
UFSD reads the member line by line. These rules are not obvious, and each
has caught somebody.

#deflist(width: 1.6in,
  [Upper case], [The words #cmd("ROOT") and #cmd("MOUNT") and the keywords are
    recognised in upper case only. #cmd("mount dsn(x.y) path(/z)") is an
    unrecognised statement. The values (data set names, paths) keep the case
    you write.],
  [A blank after the word], [The word #cmd("ROOT") or #cmd("MOUNT") must be
    followed by a blank. #cmd("MOUNT") at the end of a line is an
    unrecognised statement.],
  [Keyword and value], [A keyword must be directly followed by an opening
    parenthesis, and it must be preceded by a blank or the start of the line.
    The value runs up to the first closing parenthesis. A blank inside the
    parentheses becomes part of the value.],
  [One line], [A statement is one line of at most 511 characters.],
  [Comments], [A line whose first non-blank characters are #cmd("/*") starts a
    block comment. It ends in the first line that has #cmd("*/"), which may
    be the same one. UFSD skips all of that line: text after the #cmd("*/")
    is lost. A line whose first non-blank character is #cmd("#") is a comment
    line. A #cmd("#") anywhere else is data, because the characters
    #cmd("@"), #cmd("#") and #cmd("$") are valid in data set names. There is
    no trailing comment.],
  [Lengths], [A data set name is cut after 44 characters, a path after 127
    and an owner after 8. UFSD does not warn.],
  [The mode], [The mode is read/write only if the value begins with the
    letters #cmd("RW"). Anything else, such as #cmd("rw"), #cmd("R/W") or a
    mistyped #cmd("MODE(WR)"), is read only, without a message. Check the
    mode with #cmd("MOUNT LIST") after the start (@ug-ops-mount).],
  [Unknown statements], [A line that is neither a comment nor a #cmd("ROOT")
    or #cmd("MOUNT") statement gets #cmd("UFSD104W") with its first 40
    characters, and UFSD goes on.],
  [The block size], [The statement #cmd("ROOT") accepts #cmd("BLKSIZE(n)"),
    with a suffix #cmd("K") or #cmd("M"). UFSD ignores the value in release
    1.5.2. The block size of a file system is the one with which it was
    formatted.],
)

== When the Member Cannot Be Used <ug-config-failure>

#idx("startup failure")
Some faults in the member make the start fail, and some are only reported.

#tab(caption: [Faults in the configuration])[
  #table(columns: (2.3in, 1fr),
    [What happened], [What UFSD does],
    [The member cannot be opened (#cmd("UFSD100W")).], [The start fails. The text
      says #cmd("USING DEFAULTS"), but there are none. UFSD says
      #cmd("UFSD061E PARMLIB (DD:UFSDPRM) UNUSABLE -- SHUTTING DOWN"), and the
      step ends with RC 8.],
    [The member cannot be read to the end (#cmd("UFSD106E")).], [The same:
      #cmd("UFSD061E") and RC 8. A member that is read in part is not used.],
    [There is no #cmd("ROOT") with a #cmd("DSN()") (#cmd("UFSD105W")).], [The
      same: #cmd("UFSD061E") and RC 8.],
    [The root cannot be mounted.], [#cmd("UFSD061E CANNOT MOUNT ROOT FILESYSTEM -- SHUTTING DOWN") and RC 8. The message before it says why: a
      data set that does not exist (#cmd("UFSD120E")), a data set that is not a
      file system (#cmd("UFSD044E")), a damaged superblock (#cmd("UFSD062E"),
      #cmd("UFSD124E")).],
    [A child cannot be mounted.], [#cmd("UFSD123W CANNOT MOUNT DSN=... ON ...") and the
      same messages as for the root. UFSD starts without that file system.],
    [A statement is wrong (#cmd("UFSD101W") to #cmd("UFSD104W")).], [UFSD skips
      the statement and goes on.],
  )
]

The start of a server that comes up with a file system missing is the
worst case, because it looks healthy. After every change of the member, read
the lines #cmd("UFSD060I") and #cmd("UFSD040I") of the start (@ug-ops-start),
and check the table with #cmd("/F UFSD,MOUNT LIST").

== Examples <ug-config-examples>

A root and nothing else:

#screen(```
ROOT     DSN(UFSD.ROOT)
```)

A web server with content and a scratch area:

#screen(```
ROOT     DSN(UFSD.ROOT)
MOUNT    DSN(HTTPD.WEBROOT.UFS)  PATH(/www)          MODE(RO)
MOUNT    DSN(UFSD.SCRATCH)       PATH(/tmp)          MODE(RW)
```)

Several users, each with a home that only the user can change:

#screen(```
ROOT     DSN(UFSD.ROOT)
MOUNT    DSN(HTTPD.WEBROOT.UFS)  PATH(/www)          MODE(RO)
MOUNT    DSN(IBMUSER.UFSHOME)    PATH(/u/ibmuser)    MODE(RW) OWNER(IBMUSER)
MOUNT    DSN(MVSCE01.UFSHOME)    PATH(/u/mvsce01)    MODE(RW) OWNER(MVSCE01)
MOUNT    DSN(MVSCE02.UFSHOME)    PATH(/u/mvsce02)    MODE(RW) OWNER(MVSCE02)
MOUNT    DSN(UFSD.SCRATCH)       PATH(/tmp)          MODE(RW)
```)

A file system you do not need now can stay in the member as a comment line:

#screen(```
# MOUNT    DSN(UFSD.ARCHIVE)      PATH(/archive)     MODE(RO)
```)

A change of the member takes effect at the next start. A mount made with the
operator command lasts until UFSD stops (@ug-ops-mount).
