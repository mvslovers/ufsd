#import "../bookmaster/bookmaster.typ": *

= The UFSDPRM Statements <ref-cfg>

#idx("configuration")
#idx("UFSDPRM")
UFSD (ufsd) reads its configuration from the DD statement #cmd("UFSDPRM") of
the procedure, once, at the start. A change takes effect at the next start;
the only change that can be made to a running server is the console command
#cmd("MOUNT") (@ref-cmd-mount). The _UFSD User's Guide_ shows how to plan and
write a member. This chapter is the definition of the statements.

== The Member <ref-cfg-member>

The DD statement #cmd("UFSDPRM") names a member of a partitioned data set or a
sequential data set with record length of up to 511 characters. The procedure
in the sample library builds the name from two symbolic parameters:

#tab(caption: [The DD statement UFSDPRM in the sample procedure])[
  #table(columns: (1.4in, 1fr),
    [Parameter], [Meaning],
    [#cmd("D='SYS2.PARMLIB'")], [The data set. The procedure's default; on TK5 there is no #cmd("SYS2.PARMLIB"), so start with #cmd("S UFSD,D='SYS1.PARMLIB'") or change the default.],
    [#cmd("M=UFSDPRM0")], [The member. The sample library holds a member of this name.],
  )
]

#screen(```
//UFSDPRM  DD  DISP=SHR,DSN=&D(&M),FREE=CLOSE
```)

A record is a line. Each line is one of: empty, a comment, a #cmd("ROOT")
statement or a #cmd("MOUNT") statement. A statement is exactly one line;
there are no continuation lines. UFSD reads a line of at most 511 characters. The part of a longer line that is
beyond that is read as a new line and, as a rule, ends in #cmd("UFSD104W").
Columns are not special: the statement may begin in any column, and trailing
blanks are removed.

== Comments <ref-cfg-comments>

#idx("comments")
#tab(caption: [Comments in the member])[
  #table(columns: (1.5in, 1fr),
    [Form], [Effect],
    [A line whose first non-blank character is #cmd("#")], [The whole line is a comment.],
    [A line that begins with #cmd("/*") and contains #cmd("*/")], [The line is a comment.],
    [A line that begins with #cmd("/*") and does not contain #cmd("*/")], [A comment that goes on. Every following line is skipped, up to and including the first line that contains #cmd("*/"). Whatever else is on that last line is skipped, too.],
  )
]

There are no comments at the end of a statement line. A #cmd("#") or a
#cmd("/*") that does not begin the line is data. This is on purpose: the
characters #cmd("#"), #cmd("@") and #cmd("$") are valid in data set names, so
#cmd("DSN(MY.DATA#SET)") has to survive. Remember this when you comment out a
statement: put the #cmd("#") in front of it.

A block comment that is never closed hides the rest of the member, with no message.
If a mount you wrote is missing, look for a #cmd("/*") above it.

== Reading a Statement <ref-cfg-lex>

A statement begins with its word, #cmd("ROOT") or #cmd("MOUNT"), in upper case,
and at least one blank after it. The word must be the first word on the line,
after any blanks. A line that is not empty, not a comment, and begins with
anything else gets #cmd("UFSD104W PARMLIB: UNRECOGNIZED STATEMENT: text") with
the first 40 characters, and is skipped. #cmd("root") in lower case is
unrecognised, too.

The rest of the line holds parameters of the form #cmd("KEY(value)"). The rules
are the same for all keys:

- The key is upper case and is followed at once by the opening parenthesis. A
  blank between them (#cmd("DSN (X)")) makes UFSD overlook the key.
- The key must stand at the start of the line or after a blank. #cmd("XDSN(A)")
  is not a #cmd("DSN").
- The value is everything between the parenthesis and the first closing
  parenthesis. It is not trimmed: a blank inside the parentheses is part of
  the value. A value cannot contain a closing parenthesis. A key without a
  closing parenthesis is treated as missing.
- Parameters may come in any order. The key is searched in the whole line,
  so a key that is repeated counts at its first occurrence.
- Values are used as written, without any change of case. Write data set
  names, paths and user IDs the way the system expects them: data set
  names and user IDs in upper case.
- A value longer than the limit of the parameter is cut without a message.
  Parameters that UFSD does not know are ignored.

== ROOT <ref-cfg-root>

#idx("ROOT")
#screen(```
ROOT  DSN(dsn)
```)

#tab(caption: [The parameters of ROOT])[
  #table(columns: (1in, 1.1in, 1fr),
    [Parameter], [Limit], [Meaning],
    [#cmd("DSN")], [44 characters], [Required. The data set that holds the root file system. It must exist and have been formatted with UFSFMT.],
    [#cmd("BLKSIZE")], [], [Accepted and ignored. UFSD reads the block size from the file system. The value is converted (a #cmd("K") or #cmd("M") after the digits is understood) but is not used for anything.],
  )
]

The root is mounted at #cmd("/"). It is the only file system that has no
#cmd("PATH"), no #cmd("MODE") and no #cmd("OWNER"). UFSD opens it for update
first and then keeps it read only: files and directories cannot be created
on the root. Its only job is to hold the directories on which the other
file systems are mounted.

A member must have a #cmd("ROOT") statement with a #cmd("DSN"). If a second #cmd("ROOT")
statement follows, it replaces the first (the parameters it gives replace the
ones before; a parameter that it does not give keeps the earlier value). Without
a root, the start ends:

#screen(```
UFSD105W PARMLIB: ROOT STATEMENT MISSING
UFSD061E PARMLIB (DD:UFSDPRM) UNUSABLE -- SHUTTING DOWN
```)

== MOUNT <ref-cfg-mount>

#idx("MOUNT")
#screen(```
MOUNT  DSN(dsn)  PATH(/path)  [MODE(RO|RW)]  [OWNER(user)]
```)

#tab(caption: [The parameters of MOUNT])[
  #table(columns: (1in, 1.1in, 1fr),
    [Parameter], [Limit], [Meaning],
    [#cmd("DSN")], [44 characters], [Required. The data set of the file system. Without it: #cmd("UFSD102W"), and the statement is skipped.],
    [#cmd("PATH")], [127 characters], [Required. The mount point. It must begin with a slash. Without it: #cmd("UFSD103W"), and the statement is skipped.],
    [#cmd("MODE")], [], [#cmd("RW") or #cmd("RO"). The default is #cmd("RO"). The mount is read/write only when the value begins with the letters #cmd("RW"); anything else, including #cmd("rw") in lower case or a misspelling, silently gives #cmd("RO").],
    [#cmd("OWNER")], [8 characters], [The user ID that may write to the file system. Without it, any user may write to a read/write mount.],
  )
]

*At most 16 file systems.* The table of mounted file systems has 16 slots, and
the root takes one. The member may therefore hold 16 #cmd("MOUNT") statements,
but only 15 of them can be mounted. The 17th and later statements are not even
read: each gets #cmd("UFSD101W PARMLIB: TOO MANY MOUNT STATEMENTS (MAX 16)").
The 16th statement is read and fails at the mount with
#cmd("UFSD061E MOUNT: DISK TABLE FULL (16 SLOTS)") and #cmd("UFSD123W").

=== Mounting at the Start

After the root, UFSD mounts the other file systems in the order of the
number of path components, the shallowest first, so that a mount point can be
on a file system that was mounted before it. For each one UFSD:

+ creates the mount point directory on the file system above it, with all the
  directories on the way. If that fails, #cmd("UFSD122W") is issued and the
  mount is tried all the same;
+ allocates the data set: #cmd("DISP=OLD") for #cmd("MODE(RW)") and
  #cmd("DISP=SHR") for #cmd("MODE(RO)"). UFSD names the DD
  #cmd("UFD00001"), #cmd("UFD00002") and so on;
+ opens the data set and checks the boot block and the superblock;
+ issues #cmd("UFSD060I MOUNTED dsn ON path (mode)").

A mount that fails gives #cmd("UFSD123W CANNOT MOUNT DSN=dsn ON path"), preceded
by the message that says why, and the start goes on with the next statement.
Only a failure of the root ends the start (#cmd("UFSD061E CANNOT MOUNT ROOT
FILESYSTEM -- SHUTTING DOWN")).

=== Who May Write <ref-cfg-access>

#idx("access", "write check")
UFSD makes one check, and only for a write. A client that writes (creates,
removes, renames, truncates, or writes data) to a file system gets:

#tab(caption: [The write check])[
  #table(columns: (2.2in, 1fr),
    [Situation], [Result],
    [The mount is #cmd("RO")], [The request fails with #cmd("UFSD_RC_ROFS") (68).],
    [The mount has an #cmd("OWNER") and the user ID of the session is not the same (the comparison is exact, including case)], [The request fails with #cmd("UFSD_RC_EACCES") (72).],
    [The mount is #cmd("RW") and has no #cmd("OWNER")], [Any user may write.],
  )
]

There is no check on reading: every client can read every file of a mounted
file system. The mode, the owner and the group stored in an inode are
information; UFSD does not look at them to decide on an access. The
#cmd("OWNER") of the mount is the only protection of a read/write file system.

If #cmd("OWNER()") names another user than the owner stored in the root
directory of the data set, UFSD warns with #cmd("UFSD125W") and uses the
owner of the statement.

== A Complete Member <ref-cfg-sample>

This is the member in the sample library, #cmd("UFSD.SAMPLIB(UFSDPRM0)"):

#screen(```
/* SYS2.PARMLIB(UFSDPRM0) */

/* Root filesystem - the dataset must already exist */
ROOT     DSN(UFSD.ROOT)

/* Read-only web content */
MOUNT    DSN(HTTPD.WEBROOT.UFS)  PATH(/www)          MODE(RO)

/* User home directory, writable only by IBMUSER */
MOUNT    DSN(IBMUSER.UFSHOME)    PATH(/u/ibmuser)    MODE(RW) OWNER(IBMUSER)
MOUNT    DSN(MVSCE01.UFSHOME)    PATH(/u/mvsce01)    MODE(RW) OWNER(MVSCE01)
MOUNT    DSN(MVSCE02.UFSHOME)    PATH(/u/mvsce02)    MODE(RW) OWNER(MVSCE02)

/* Shared scratch area, writable by any authenticated user */
MOUNT    DSN(UFSD.SCRATCH)       PATH(/tmp)          MODE(RW)

# MOUNT    DSN(UFSD.ARCHIVE)      PATH(/archive)     MODE(RO)
```)

== Errors in the Member <ref-cfg-errors>

#tab(caption: [What UFSD does with a faulty member])[
  #table(columns: (2.2in, 1fr),
    [Fault], [Result],
    [The DD statement is missing, or the member cannot be opened], [#cmd("UFSD100W"), then #cmd("UFSD061E PARMLIB (DD:UFSDPRM) UNUSABLE -- SHUTTING DOWN"). The step ends with return code 8. Despite the text of #cmd("UFSD100W"), UFSD does not use defaults.],
    [No #cmd("ROOT") with a #cmd("DSN")], [#cmd("UFSD105W"), then #cmd("UFSD061E"), return code 8.],
    [An input/output error while reading], [#cmd("UFSD106E"), then #cmd("UFSD061E"), return code 8. UFSD does not start with the part it has read.],
    [A statement that is neither #cmd("ROOT") nor #cmd("MOUNT")], [#cmd("UFSD104W"). The line is skipped; the start goes on.],
    [A #cmd("MOUNT") without #cmd("DSN()") or without #cmd("PATH()")], [#cmd("UFSD102W") or #cmd("UFSD103W"). The statement is skipped.],
    [More than 16 #cmd("MOUNT") statements], [#cmd("UFSD101W") for each one after the 16th.],
    [The root cannot be mounted], [The message of the cause (#cmd("UFSD120E"), #cmd("UFSD043E"), #cmd("UFSD044E"), #cmd("UFSD062E"), #cmd("UFSD124E")), then #cmd("UFSD061E CANNOT MOUNT ROOT FILESYSTEM -- SHUTTING DOWN"), return code 8.],
    [Another file system cannot be mounted], [The message of the cause, then #cmd("UFSD123W"). The start goes on.],
  )
]

A warning in the table is only a warning: the start continues and ends in
#cmd("UFSD001I"). Check the console for #cmd("UFSD1") messages after every
change to the member, and compare the answer of #cmd("F UFSD,MOUNT LIST") with
what you meant to write.
