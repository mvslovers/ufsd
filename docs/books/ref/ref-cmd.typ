#import "../bookmaster/bookmaster.typ": *

= Operator Commands <ref-cmd>

#idx("commands", "operator")
#idx("MODIFY")
This chapter describes every command that UFSD (ufsd) accepts while it runs.
The commands are given with the MODIFY console command, #cmd("F UFSD,command"),
and UFSD answers with console messages. The stop command #cmd("P UFSD") is also
treated as a command (@ref-cmd-shutdown). How to use the commands is the
subject of the _UFSD User's Guide_.

== How a Command Is Read <ref-cmd-read>

UFSD takes the text after the comma of the MODIFY command, cuts it at 63
characters, folds it to upper case and removes trailing blanks. The command is
then compared with the list below. The comparison is exact: #cmd("STATS"),
#cmd("SESSIONS"), #cmd("HELP"), #cmd("SHUTDOWN") and #cmd("REBUILD") must be the
whole text; #cmd("SESSIONS "), #cmd("MOUNT "), #cmd("UNMOUNT ") and
#cmd("TRACE ") must be followed by an operand. Anything else gets
#cmd("UFSD021E UNKNOWN COMMAND: text") followed by the list of commands
(#cmd("UFSD020I COMMANDS: ...")). That includes a #cmd("MOUNT"), #cmd("UNMOUNT")
or #cmd("TRACE") without an operand: the blank that the command word needs is
removed with the trailing blanks, so UFSD does not recognise it, and the text
after #cmd("UNKNOWN COMMAND:") is that bare word.

Because of the folding, the operands are upper case. A path that you give in
a mount command is upper case too (@ref-cmd-mount). The paths of the
configuration member keep their case.

All answers are console messages of the kind #cmd("UFSDnnnx"), listed in
@apx-messages. They are plain WTO messages without a routing code of their own.

#tab(caption: [The commands of UFSD])[
  #table(columns: (3.3in, 1fr),
    [Command], [Purpose],
    [#cmd("STATS")], [Counters, pools, mounted file systems (@ref-cmd-stats).],
    [#cmd("SESSIONS")], [The active sessions (@ref-cmd-sessions).],
    [#cmd("SESSIONS PRUNE")], [Releases sessions of ended address spaces.],
    [#cmd("MOUNT LIST")], [The mounted file systems (@ref-cmd-mount).],
    [#cmd("MOUNT DSN=dsn,PATH=/path[,MODE=RW][,OWNER=user]")], [Mounts a file system.],
    [#cmd("UNMOUNT PATH=/path")], [Unmounts a file system (@ref-cmd-umount).],
    [#cmd("TRACE ON"), #cmd("TRACE OFF"), #cmd("TRACE DUMP")], [The request trace (@ref-cmd-trace).],
    [#cmd("REBUILD")], [Rebuilds the free caches (@ref-cmd-rebuild).],
    [#cmd("HELP")], [Lists the commands (@ref-cmd-help).],
    [#cmd("SHUTDOWN")], [Stops UFSD (@ref-cmd-shutdown).],
  )
]

== STATS <ref-cmd-stats>

#idx("STATS")
*Syntax:* #cmd("F UFSD,STATS")

The answer is nine or more lines, in this order:

#screen(```
UFSD010I STATUS: status
UFSD011I CSA FREE REQS:  free/total
UFSD012I CSA FREE BUFS:  free/total
UFSD013I TRACE ENTRIES:  n
UFSD014I REQUESTS SERVED: n
UFSD015I ERRORS:          n
UFSD017I POSTS SAVED:     n
UFSD016I IN FLIGHT:       n
UFSD018I MOUNT path DSN=dsn mode FREEBLK=a/b FREEINODE=a/b
```)

Mind the order: #cmd("UFSD017I") comes before #cmd("UFSD016I"). The lines
#cmd("UFSD011I") to #cmd("UFSD016I") are not printed if UFSD has no anchor
(this does not happen in a running server). There is one #cmd("UFSD018I") line
for each mounted file system.

#tab(caption: [The lines of STATS])[
  #table(columns: (1.2in, 1fr),
    [Line], [Value],
    [#cmd("UFSD010I")], [#cmd("ACTIVE") while UFSD accepts requests, #cmd("INACTIVE") after a stop was asked for.],
    [#cmd("UFSD011I")], [Free request blocks and all request blocks (32 in release 1.5.2). Equal when no request is under way.],
    [#cmd("UFSD012I")], [Free 4096-byte data buffers and all of them (16).],
    [#cmd("UFSD013I")], [The number of entries of the trace table (256).],
    [#cmd("UFSD014I")], [Requests handled since the start, not counting those that ended in a hard error; #cmd("UFSD015I") counts these.],
    [#cmd("UFSD015I")], [Requests that ended in a hard error: input/output error, no space, no inodes, a damaged request block, an unknown function code, an invalid session or argument. A missing file, an existing name and a refused write are normal answers and are not counted.],
    [#cmd("UFSD017I")], [Requests that were put on a queue that was not empty, so that the server did not have to be posted. A measure of how busy the server is.],
    [#cmd("UFSD016I")], [Requests inside the router at this moment.],
    [#cmd("UFSD018I")], [One file system. The path is padded to 12 characters and the data set name to 20; a longer name is not cut, so the columns after it move. The mode is #cmd("RO"), #cmd("RW"), or #cmd("RW(owner)") when the mount has an owner.],
  )
]

*FREEBLK and FREEINODE.* Each shows two numbers, #cmd("a/b"). The second, #cmd("b"),
is the number of free blocks (or inodes) of the whole file system. The first,
#cmd("a"), is the number of entries in the list of free blocks (or inodes)
that UFSD keeps in memory. That list has room for 51 blocks and 64
inodes, so #cmd("a") never exceeds these numbers, and it is not a free space
that you can measure. UFSD refills the list from the disk when it runs empty
(#cmd("UFSD070I"), #cmd("UFSD076I")). Read the free space from #cmd("b").

== SESSIONS <ref-cmd-sessions>

#idx("SESSIONS")
*Syntax:* #cmd("F UFSD,SESSIONS") or #cmd("F UFSD,SESSIONS PRUNE")

#cmd("SESSIONS") lists the sessions that are open:

#screen(```
UFSD050I ACTIVE SESSIONS: n
UFSD051I   #n TOKEN=hhhhhhhh ASID=hhhh OWNER=owner    GROUP=group    FDS=n/64
```)

There is one #cmd("UFSD051I") line for each session. #cmd("#n") is the
number of the slot (counting from 1), the token identifies the session, and
#cmd("ASID") is the address space of the client, in hexadecimal. Owner and
group are the names that the client gave when it opened the session, padded
or cut to 8 characters, or #cmd("(none)"). #cmd("FDS") is the number of open
files and the most a session may have. Without a session table, the answer is
#cmd("UFSD050I ACTIVE SESSIONS: 0").

#cmd("SESSIONS PRUNE") releases every session whose address space no longer
exists. It answers with a #cmd("UFSD052I") line for each session it releases,
and then with #cmd("UFSD053I SESSIONS PRUNE: n STALE SESSION(S) RELEASED"). Any
other operand gets #cmd("UFSD021E SESSIONS: SYNTAX: ...").

== MOUNT and MOUNT LIST <ref-cmd-mount>

#idx("MOUNT")
*Syntax:*

#screen(```
F UFSD,MOUNT LIST
F UFSD,MOUNT DSN=dsn,PATH=/path[,MODE=RW][,OWNER=user]
```)

#cmd("MOUNT LIST") answers with one header and one line for each file system:

#screen(```
UFSD068I n FILESYSTEM(S) MOUNTED:
UFSD069I   path          DSN=dsn                                           RW OWNER=owner
```)

The path is padded to 12 characters (it reads #cmd("(none)") if empty) and the
data set name to 44. The mode is #cmd("RW") or #cmd("RO"), and #cmd("OWNER=")
appears only if the mount has an owner.

#cmd("MOUNT DSN=...") mounts a data set while UFSD runs.

#tab(caption: [The operands of MOUNT])[
  #table(columns: (1in, 1fr),
    [Operand], [Meaning],
    [#cmd("DSN=dsn")], [Required. The data set name, up to 44 characters. It ends at the next comma.],
    [#cmd("PATH=/path")], [Required. The mount point, up to 127 characters. It ends at the next comma. It must begin with a slash.],
    [#cmd("MODE=RW")], [Optional. The mount is read/write only if the value begins with #cmd("RW"). Any other value, and no #cmd("MODE=") at all, mounts it read only.],
    [#cmd("OWNER=user")], [Optional. The user ID that may write, up to 8 characters.],
  )
]

The operands are found by their names anywhere in the text, and each ends
at a comma. Write the command without blanks around the commas. The path is
upper case (@ref-cmd-read). UFSD does not create the directory of the mount
point in this command; the mount point of a configuration member is created
at the start, if it is missing (@ref-cfg-mount). The mount is not saved:
it is gone when UFSD stops.

*UFSD does not check whether the data set is mounted already.* Only the path
is compared, and a path that differs in any character is a different path. The
command #cmd("F UFSD,MOUNT DSN=UFSD.SCRATCH,PATH=/tmp,MODE=RW") on a system that
has this data set on #cmd("/tmp") gives the answer #cmd("UFSD060I MOUNTED
UFSD.SCRATCH ON /TMP (RW)"), because the console made the path upper case. The
data set is then open twice for writing, with two copies of its superblock in
storage, and each copy will be written back. Check #cmd("MOUNT LIST") before
you mount, and never mount a read/write data set twice.

*Answers.* On success, #cmd("UFSD060I MOUNTED dsn ON path (mode)"), with
#cmd(", OWNER=owner") if an owner was given. If the mount point does not exist
in the file system above it, #cmd("UFSD126W MOUNT POINT path NOT FOUND ON PARENT
DISK") comes first, and the mount is made all the same. The causes of failure are the
messages #cmd("UFSD021E") (the syntax, #cmd("PATH= NOT FOUND"),
#cmd("DSN= NOT FOUND")), #cmd("UFSD065E"), #cmd("UFSD067E"),
#cmd("UFSD061E MOUNT: DISK TABLE FULL"), #cmd("UFSD120E"), #cmd("UFSD042E"),
#cmd("UFSD043E"), #cmd("UFSD044E"), #cmd("UFSD049E"), #cmd("UFSD062E") and
#cmd("UFSD124E"). UFSD recognises #cmd("MOUNT LIST") only with exactly one
blank between the words; with two blanks the text is read as a mount and ends
in #cmd("UFSD021E MOUNT: DSN= NOT FOUND"). A mount whose owner differs from the owner stored in the
root directory of the disk is mounted with the warning #cmd("UFSD125W").
@apx-messages explains each of them.

== UNMOUNT <ref-cmd-umount>

#idx("UNMOUNT")
*Syntax:* #cmd("F UFSD,UNMOUNT PATH=/path") or #cmd("F UFSD,UNMOUNT /path")

UFSD reads everything after the command word as the path, to a length of 127
characters. It looks for #cmd("PATH=") in the text; if it is there, the path
begins after it. Otherwise the whole text is the path. The path is upper case
(@ref-cmd-read).

The file system is unmounted in these steps: #cmd("UFSD064I UNMOUNTING dsn FROM
path"), the superblock of a file system that is not read only is written back
(#cmd("UFSD131I") or #cmd("UFSD130W")), and the data set is freed. The root
cannot be unmounted (#cmd("UFSD132E")); a path with nothing mounted on it gets
#cmd("UFSD133E"), and #cmd("PATH=") with nothing after it gets
#cmd("UFSD021E UNMOUNT: PATH IS EMPTY"). UFSD does not check whether a client has a file open on the
file system. Unmount only when no client uses it.

== TRACE <ref-cmd-trace>

#idx("TRACE")
*Syntax:* #cmd("F UFSD,TRACE ON"), #cmd("F UFSD,TRACE OFF"), #cmd("F UFSD,TRACE DUMP")

#cmd("TRACE ON") and #cmd("TRACE OFF") switch the recording of requests and
answer #cmd("UFSD083I TRACE ENABLED") or #cmd("UFSD083I TRACE DISABLED"). The
table keeps 256 entries; a full table is overwritten from the oldest entry.
The recording is on from the start of UFSD.
Any other operand gets #cmd("UFSD021E TRACE: SYNTAX: ...").

#cmd("TRACE DUMP") prints the table:

#screen(```
UFSD080I TRACE DUMP: n ENTRies
UFSD081I   nnn: T=hhhhhhhh F=hhhh RC=hhhh SESS=hhhhhhhh
```)

There is one #cmd("UFSD081I") line for each entry that holds data, oldest first.
#cmd("T") is the low word of the time-of-day clock, #cmd("F") the kind of
event, #cmd("RC") a code that depends on the event, and #cmd("SESS") the token
of the session. All four are in hexadecimal.

#tab(caption: [The events of the trace])[
  #table(columns: (0.7in, 1.6in, 1fr),
    [F], [Event], [What RC holds],
    [0001], [A request arrived.], [The function code of the request.],
    [0002], [The request is complete.], [The return code of the request
      (@ref-lim-rc).],
    [0010], [A session was opened.], [0.],
    [0011], [A session was closed.], [0.],
    [0012], [A session was cleaned up after the end of its address space.], [0.],
    [00F0], [A damaged request block.], [The return code CORRUPT.],
    [00F1], [An invalid function code.], [The return code BADFUNC.],
    [00F2], [An invalid session token.], [The return code BADSESS.],
  )
]
 The ending of the header is lower
case: #cmd("ENTRy") for one entry, #cmd("ENTRies") for any other number. Without a
table, the answer is #cmd("UFSD080W TRACE DUMP: TRACE BUFFER NOT AVAILABLE").

The table is meant for the support of the product, not for the operator.

== REBUILD <ref-cmd-rebuild>

#idx("REBUILD")
*Syntax:* #cmd("F UFSD,REBUILD")

REBUILD scans the inodes of every read/write file system and rebuilds the lists
of free blocks and free inodes in memory from the blocks and inodes that the
files use. It does not repair files that were stored wrongly before, and it
makes no promise about the state of the free lists afterwards. For each file
system it answers:

#screen(```
UFSD073I REBUILD: SCANNING ddname ...
UFSD074I REBUILD: ddname DONE, FREEBLK=n FREEINODE=n
```)

A file system mounted read only gets #cmd("UFSD072I REBUILD: SKIPPING ddname
(READ-ONLY)") and is not scanned. The root is the exception: #cmd("MOUNT LIST")
shows it as #cmd("RO"), but REBUILD scans it all the same. A failed scan gets #cmd("UFSD075E ... FAILED
RC=n") and the command goes on with the next file system. The scan messages
#cmd("UFSD070I") and #cmd("UFSD076I") may appear in between. Without a
mounted file system, the answer is #cmd("UFSD071W"). The ddname in the messages
is the name that UFSD allocated for the data set, not the data set name.

UFSD does one thing at a time. While REBUILD runs, no client request is
handled, so on a large disk run it at a quiet time.

== HELP <ref-cmd-help>

*Syntax:* #cmd("F UFSD,HELP")

HELP answers with the one-line list that every unknown command gets, followed
by the syntax of six commands, all as #cmd("UFSD020I"):

#screen(```
UFSD020I COMMANDS: STATS, SESSIONS, MOUNT, UNMOUNT, TRACE, REBUILD, HELP, SHUTDOWN
UFSD020I   MOUNT DSN=dsn,PATH=/path~,MODE=RW~~,OWNER=user~
UFSD020I   MOUNT LIST
UFSD020I   UNMOUNT PATH=/path
UFSD020I   TRACE ON|OFF|DUMP
UFSD020I   SESSIONS PRUNE  -- RELEASE STALE SESSIONS (TERMINATED ASIDS)
UFSD020I   REBUILD  -- SCAN INODES, REBUILD FREE BLOCK/INODE CACHE
```)

The text above is what the console of TK5 shows. UFSD sends square brackets
in the second line, around the optional operands, and the console shows each
of them as a tilde. Read the line as
#cmd("MOUNT DSN=dsn,PATH=/path[,MODE=RW][,OWNER=user]").

== SHUTDOWN and P UFSD <ref-cmd-shutdown>

#idx("SHUTDOWN")
*Syntax:* #cmd("F UFSD,SHUTDOWN") or #cmd("P UFSD")

Both ask the same stop. UFSD stops accepting requests, so that #cmd("STATS")
then reads #cmd("INACTIVE"), and the main loop ends the server. The messages of
the stop are listed in @ref-stc-stop. The command itself gives no answer of
its own; the first message is #cmd("UFSD098I UFSD SHUTTING DOWN").
