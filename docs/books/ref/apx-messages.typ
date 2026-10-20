#import "../bookmaster/bookmaster.typ": *

= Messages <apx-messages>

#idx("messages", "list")
This appendix lists every message that UFSD, its cleanup program UFSDCLNP and
the format utility UFSFMT can issue, in the order of the numbers. Each text is
the one in the code of release 1.5.0.

A message of UFSD or UFSDCLNP has the form #cmd("UFSDnnnx"), a message of the
format utility #cmd("UFSFMTnnx"). The letter at the end is the severity:
#cmd("I") information, #cmd("W") warning, #cmd("E") error. Some numbers are used with more than one letter (#cmd("UFSD045E") and
#cmd("UFSD045I") are different messages), so look for the whole id.

In the texts, #cmd("n") stands for a number, #cmd("xxxx") for a number in
hexadecimal, #cmd("dsn") for a data set name, #cmd("ddname") for a DD name,
#cmd("path") for a path, #cmd("owner") and #cmd("group") for a name, and
#cmd("text") for text that varies.

== UFSD and UFSDCLNP <apx-messages-ufsd>

All messages of UFSD and UFSDCLNP are plain WTO messages to the console. UFSD
sets no routing code and no descriptor code. The texts are in upper case except
where a text shows a value as it is. A line is never longer than about 124
characters. UFSDCLNP has no prefix of its own; its messages are the numbers 140
to 157, shared with the reclaim of the start. An id can have more than one
text; each text has its own line. A number that UFSD uses for the success and
for the failure of one thing (for example #cmd("UFSD046E") and
#cmd("UFSD046I")) has one line for each id.

Not every message of the E kind ends the start. Where the start ends, the
table says "return code 8"; an E message about one file system, for instance
the failed mount of a file system other than the root, lets the start go on.


=== Start and Stop (000 to 009) <apx-messages-start>

#tab(caption: [Messages 000 to 009])[
  #table(columns: (2.9in, 1fr),
    [Message], [Meaning and action],
    [#cmd("UFSD000I UFSD version (commit) STARTING")], [The first message of every start. It carries the version and the commit of the build, in upper case.],
    [#cmd("UFSD001I UFSD version READY -- n DISKS, nK CSA, n SESSIONS, n FILES")], [The start is complete and UFSD serves clients. The numbers are the file systems mounted, the common storage taken in kilobytes, the session slots and the file slots.],
    [#cmd("UFSD002E UFSD ALREADY REGISTERED AND ACTIVE -- NOT STARTING (IF NOT RUNNING, RUN UFSDCLNP)")], [The start found the anchor of an earlier run that is flagged active, and it could not free it: the server is running (UFSD155W before it), or the address space could not be checked (UFSD156W). Return code 8. If no UFSD is running, see "Starting Again" and "The Cleanup Program UFSDCLNP" in the _UFSD User's Guide_.],
    [#cmd("UFSD003E PREDECESSOR RECLAIM FAILED -- NOT STARTING")], [The start could not free the storage of an earlier run, because it could not enter supervisor state (UFSD143E before it). Return code 8.],
    [#cmd("UFSD004I ORPHANED PREDECESSOR RECLAIMED -- CSA RECOVERED")], [The start found and freed the common storage of an earlier run that ended without freeing it. The messages UFSD144I to UFSD148I come before it. The start goes on.],
    [#cmd("UFSD005I text")], [The second line of the banner: the version of the LIBC/370 that UFSD was linked with, in upper case.],
    [#cmd("UFSD006W BUILT FROM A MODIFIED WORKING TREE")], [Issued only by a build made from a changed source tree. A release does not issue it.],
    [#cmd("UFSD007I AUTHORIZED BY LIBRARY (MODULE KEY 0)")], [UFSD was already authorised when it started, because its library is on the APF list. The load module is fetched in key 0.],
    [#cmd("UFSD007I AUTHORIZED BY SVC (MODULE KEY 8)")], [UFSD was not authorised at entry and became authorised through the SVC of RAKF. The load module is in key 8.],
  )
]


=== Operator Commands and Early Start Errors (010 to 027) <apx-messages-cmd>

The answers to the MODIFY command are described with the commands in @ref-cmd.

#tab(caption: [Messages 010 to 027])[
  #table(columns: (2.9in, 1fr),
    [Message], [Meaning and action],
    [#cmd("UFSD010I STATUS: status")], [First line of the answer to STATS. The status is ACTIVE or INACTIVE.],
    [#cmd("UFSD011I CSA FREE REQS:  free/total")], [STATS: the free request blocks and the total.],
    [#cmd("UFSD012I CSA FREE BUFS:  free/total")], [STATS: the free data buffers and the total.],
    [#cmd("UFSD013I TRACE ENTRIES:  n")], [STATS: the size of the trace table.],
    [#cmd("UFSD014I REQUESTS SERVED: n")], [STATS: the requests handled since the start.],
    [#cmd("UFSD015I ERRORS:          n")], [STATS: the hard errors since the start. A refused write or a missing file does not count.],
    [#cmd("UFSD017I POSTS SAVED:     n")], [STATS: how often a request was put on a queue that was not empty, so that the server did not have to be posted. Printed before UFSD016I.],
    [#cmd("UFSD016I IN FLIGHT:       n")], [STATS: the requests in the router at this moment.],
    [#cmd("UFSD018I MOUNT path DSN=dsn RW(owner) FREEBLK=free/total FREEINODE=free/total")], [STATS: the line of a read/write file system that has an owner.],
    [#cmd("UFSD018I MOUNT path DSN=dsn mode FREEBLK=free/total FREEINODE=free/total")], [STATS: the line of any other file system. The mode is RO or RW. The path is padded to 12 characters, the data set name to 20.],
    [#cmd("UFSD020I COMMANDS: STATS, SESSIONS, MOUNT, UNMOUNT, TRACE, REBUILD, HELP, SHUTDOWN")], [The list of commands. It follows UFSD021E UNKNOWN COMMAND, and it is the first line of the answer to HELP.],
    [#cmd("UFSD020I   MOUNT DSN=dsn,PATH=/path[,MODE=RW][,OWNER=user]")], [HELP: the syntax of MOUNT.],
    [#cmd("UFSD020I   MOUNT LIST")], [HELP: the syntax of MOUNT LIST.],
    [#cmd("UFSD020I   UNMOUNT PATH=/path")], [HELP: the syntax of UNMOUNT.],
    [#cmd("UFSD020I   TRACE ON|OFF|DUMP")], [HELP: the syntax of TRACE.],
    [#cmd("UFSD020I   SESSIONS PRUNE  -- RELEASE STALE SESSIONS (TERMINATED ASIDS)")], [HELP: the syntax of SESSIONS PRUNE.],
    [#cmd("UFSD020I   REBUILD  -- SCAN INODES, REBUILD FREE BLOCK/INODE CACHE")], [HELP: the syntax of REBUILD.],
    [#cmd("UFSD021E UNKNOWN COMMAND: text")], [UFSD does not know the command. The text is the command as UFSD read it: folded to upper case, cut at 63 characters, without trailing blanks. A MOUNT, UNMOUNT or TRACE without an operand ends here, too. The command list UFSD020I follows.],
    [#cmd("UFSD021E SESSIONS: SYNTAX: SESSIONS  OR  SESSIONS PRUNE")], [SESSIONS was given an operand other than PRUNE.],
    [#cmd("UFSD021E MOUNT: SYNTAX: MOUNT DSN=dsn,PATH=/path  OR  MOUNT LIST")], [MOUNT without an operand. A bare MOUNT from the console ends in UFSD021E UNKNOWN COMMAND instead, so this message is not seen in practice.],
    [#cmd("UFSD021E MOUNT: PATH= NOT FOUND")], [MOUNT has DSN= but no PATH=. Nothing is mounted.],
    [#cmd("UFSD021E MOUNT: DSN= NOT FOUND")], [MOUNT has an operand that is not LIST and has no DSN=. Nothing is mounted.],
    [#cmd("UFSD021E UNMOUNT: SYNTAX: UNMOUNT PATH=/path")], [UNMOUNT without an operand. Not seen in practice, for the same reason as the MOUNT form.],
    [#cmd("UFSD021E UNMOUNT: PATH IS EMPTY")], [UNMOUNT PATH= with no path after the equal sign.],
    [#cmd("UFSD021E TRACE: SYNTAX: TRACE ON|OFF|DUMP")], [TRACE was given an operand other than ON, OFF or DUMP.],
    [#cmd("UFSD025E CANNOT INSTALL SSCT, RC=n")], [The subsystem UFS1 could not be registered. The code is the return code of the registration. Return code 8. The storage already taken is freed.],
    [#cmd("UFSD026E CANNOT ENTER SUPERVISOR FOR SSI LOAD")], [UFSD could not enter supervisor state to load the SSI router. Return code 8.],
    [#cmd("UFSD027E CANNOT LOAD UFSDSSIR INTO CSA, RC=n")], [The router module UFSDSSIR could not be loaded into the common storage. Return code 8.],
  )
]


=== Resources Taken and Freed (030 to 049) <apx-messages-init>

#tab(caption: [Messages 030 to 049])[
  #table(columns: (2.9in, 1fr),
    [Message], [Meaning and action],
    [#cmd("UFSD030I CSA ALLOCATED: ANCHOR=address")], [The anchor in the common storage was allocated. The address is the one that a dump shows.],
    [#cmd("UFSD031I   REQUEST POOL: n BLOCKS, nK")], [The size of the pool of request blocks.],
    [#cmd("UFSD032I   BUFFER POOL:  n BLOCKS, nK")], [The size of the pool of data buffers.],
    [#cmd("UFSD033I   TRACE BUFFER: n ENTRIES, nK")], [The size of the trace table.],
    [#cmd("UFSD034I SSCT REGISTERED, SUBSYSTEM NAME=name")], [The subsystem is registered. The name is UFS1.],
    [#cmd("UFSD035I SSI ROUTER LOADED AT address")], [The router UFSDSSIR is in the common storage at this address.],
    [#cmd("UFSD036I SSI ROUTER UNLOADED")], [A stop freed the router.],
    [#cmd("UFSD038E CANNOT ENTER SUPERVISOR STATE FOR CSA INIT")], [UFSD could not enter supervisor state to allocate the common storage. Return code 8.],
    [#cmd("UFSD039E CANNOT ALLOCATE CSA POOLS")], [The request pool, the buffer pool or the trace table could not be allocated in the common storage. Return code 8. Look at the free common storage of the system.],
    [#cmd("UFSD040I n FILESYSTEM(S) MOUNTED")], [All mount statements were tried. The count includes the root. The last message before UFSD001I.],
    [#cmd("UFSD042E CANNOT ALLOCATE DISK HANDLE FOR ddname")], [There is no storage for the control block of the file system. The mount fails.],
    [#cmd("UFSD042E CANNOT ALLOCATE DCB FOR ddname")], [There is no storage for the data control block. The mount fails.],
    [#cmd("UFSD043E CANNOT OPEN ddname")], [The data set could not be opened with BDAM. The mount fails. The DD name is the one that UFSD allocated for the data set.],
    [#cmd("UFSD044E ddname: NOT A VALID UFS DISK (TYPE=xxxx)")], [Block 0 of the data set is not the boot block of a UFS file system. The data set was not formatted with UFSFMT, or it is not the one you meant. The mount fails. The value is the type found in the block.],
    [#cmd("UFSD045E CANNOT ALLOCATE SESSION TABLE")], [No storage for the session table. Return code 8.],
    [#cmd("UFSD045I SESSION TABLE: n SLOTS")], [The session table is ready. UFSD 1.5.0 has 64 slots.],
    [#cmd("UFSD046E CANNOT ENTER SUPERVISOR STATE FOR SESSION INIT")], [Supervisor state could not be entered for the session table. Return code 8.],
    [#cmd("UFSD046I SESSION TABLE FREED")], [A stop freed the session table.],
    [#cmd("UFSD047E CANNOT ALLOCATE GLOBAL FILE TABLE")], [No storage for the table of open files. Return code 8.],
    [#cmd("UFSD047I GLOBAL FILE TABLE: n SLOTS")], [The table of open files is ready. UFSD 1.5.0 has 256 slots.],
    [#cmd("UFSD048E CANNOT ENTER SUPERVISOR STATE FOR GFT INIT")], [Supervisor state could not be entered for the table of open files. Return code 8.],
    [#cmd("UFSD048I GLOBAL FILE TABLE FREED")], [A stop freed the table of open files.],
    [#cmd("UFSD049E ddname: NO STORAGE FOR BOOT BLOCK (n BYTES)")], [There was no storage to read block 0 of the file system. The mount fails.],
  )
]


=== Sessions (050 to 059) <apx-messages-sess>

#tab(caption: [Messages 050 to 059])[
  #table(columns: (2.9in, 1fr),
    [Message], [Meaning and action],
    [#cmd("UFSD050I ACTIVE SESSIONS: 0")], [The answer to SESSIONS when UFSD has no session table. Nothing follows.],
    [#cmd("UFSD050I ACTIVE SESSIONS: n")], [The answer to SESSIONS. One UFSD051I follows for each active session.],
    [#cmd("UFSD051I   #n TOKEN=token ASID=asid OWNER=owner GROUP=group FDS=open/max")], [One session. The owner and the group are the names the client gave, or (none). FDS is the number of open files and the most the session may have (64).],
    [#cmd("UFSD052I CLEANUP: SESSION #n TOKEN=token ASID=asid (owner) RELEASED")], [SESSIONS PRUNE released a session whose address space no longer exists.],
    [#cmd("UFSD053I SESSIONS PRUNE: n STALE SESSION(S) RELEASED")], [The total of SESSIONS PRUNE.],
  )
]


=== Mounts, Free Lists and Rebuild (060 to 079) <apx-messages-mount>

#tab(caption: [Messages 060 to 079])[
  #table(columns: (2.9in, 1fr),
    [Message], [Meaning and action],
    [#cmd("UFSD060I MOUNTED dsn ON path (mode, OWNER=owner)")], [A file system is mounted. The mode is ROOT for the root, otherwise RW or RO. The owner is shown if the mount has one.],
    [#cmd("UFSD060I MOUNTED dsn ON path (mode)")], [The same, for a mount without an owner.],
    [#cmd("UFSD061E MOUNT: DISK TABLE FULL (n SLOTS)")], [The table of mounted file systems has 16 slots, the root included. The mount fails, and a UFSD123W follows when the statement was in the configuration.],
    [#cmd("UFSD061E PARMLIB (DD:UFSDPRM) UNUSABLE -- SHUTTING DOWN")], [The configuration could not be read, or has no ROOT statement. The reason is in the message before it: UFSD100W, UFSD105W or UFSD106E. UFSD ends with return code 8.],
    [#cmd("UFSD061E CANNOT MOUNT ROOT FILESYSTEM -- SHUTTING DOWN")], [The root data set could not be mounted. The message before it says why (UFSD043E, UFSD044E, UFSD120E, UFSD124E). UFSD ends with return code 8.],
    [#cmd("UFSD062E SUPERBLOCK VALIDATION FAILED FOR dsn")], [The superblock of the data set was read but a field in it is wrong. The data set is damaged or is not a file system of this version. UFSD124E follows.],
    [#cmd("UFSD063E I/O ERROR ON DSN=dsn BLOCK=n (READ)")], [A block could not be read. UFSD gives this message once for each mounted file system, however many errors follow. The block number is the sector.],
    [#cmd("UFSD063E I/O ERROR ON DSN=dsn BLOCK=n (WRITE)")], [A block could not be written. Once for each mounted file system, as the read form.],
    [#cmd("UFSD064I UNMOUNTING dsn FROM path")], [The answer to UNMOUNT, before the superblock is written back. Only UNMOUNT issues it. A stop writes the file systems back with UFSD130W or UFSD131I.],
    [#cmd("UFSD065E MOUNT: PATH MUST BE ABSOLUTE")], [The path of a mount does not begin with a slash. The mount fails.],
    [#cmd("UFSD067E MOUNT: path ALREADY MOUNTED")], [Another file system is mounted at this path. The mount fails.],
    [#cmd("UFSD068I n FILESYSTEM(S) MOUNTED:")], [The first line of the answer to MOUNT LIST.],
    [#cmd("UFSD069I   path  DSN=dsn  mode OWNER=owner")], [MOUNT LIST: one line for each file system. The path is padded to 12 characters (it reads (none) when empty), the data set name to 44. The mode is RW or RO. OWNER= appears only if the mount has one.],
    [#cmd("UFSD070E CANNOT ALLOCATE UFS HANDLE")], [There was no storage for the control block of a new session. The session is not opened and the client gets an error.],
    [#cmd("UFSD070I FREE BLOCK SCAN: FOUND n BLOCKS (CACHE REFILLED TO n)")], [The cache of free blocks ran dry, and UFSD scanned the inodes to refill it. It appears during a write by a client, and during REBUILD. It is not an error.],
    [#cmd("UFSD071W REBUILD: NO DISKS MOUNTED")], [REBUILD found no mounted file system.],
    [#cmd("UFSD072I REBUILD: SKIPPING ddname (READ-ONLY)")], [REBUILD does not scan a read only file system.],
    [#cmd("UFSD073I REBUILD: SCANNING ddname ...")], [REBUILD starts to scan a read/write file system.],
    [#cmd("UFSD074I REBUILD: ddname DONE, FREEBLK=n FREEINODE=n")], [The scan of a file system is over. The numbers are the free blocks and inodes after it.],
    [#cmd("UFSD075E REBUILD: ddname FAILED RC=n")], [The scan failed. The code is 44 (no free block found), 52 (input/output error) or the code of the superblock write. REBUILD goes on with the next file system.],
    [#cmd("UFSD076I FREE INODE SCAN: FOUND n INODES (CACHE REFILLED TO n)")], [The cache of free inodes ran dry and was refilled by a scan. Like UFSD070I, it appears during a write or a REBUILD and is not an error.],
  )
]


=== Trace, Failure at Start and Stop (080 to 099) <apx-messages-trace>

#tab(caption: [Messages 080 to 099])[
  #table(columns: (2.9in, 1fr),
    [Message], [Meaning and action],
    [#cmd("UFSD080I TRACE DUMP: n ENTRies")], [The first line of TRACE DUMP. The ending is written in lower case: ENTRy for one entry, ENTRies for any other number. One UFSD081I follows for each entry.],
    [#cmd("UFSD080W TRACE DUMP: TRACE BUFFER NOT AVAILABLE")], [TRACE DUMP found no trace table.],
    [#cmd("UFSD081I   n: T=time F=function RC=result SESS=session")], [One trace entry, oldest first. The number is a sequence number; the time stamp, the function code, the result and the session token are in hexadecimal.],
    [#cmd("UFSD083I TRACE ENABLED")], [The answer to TRACE ON.],
    [#cmd("UFSD083I TRACE DISABLED")], [The answer to TRACE OFF.],
    [#cmd("UFSD090E UNABLE TO INITIALIZE CONSOLE INTERFACE")], [UFSD could not get the console communication area. Return code 8. This message and UFSD091E can come before UFSD000I.],
    [#cmd("UFSD091E CANNOT ENTER SUPERVISOR STATE FOR SSCT INIT")], [Supervisor state could not be entered to register the subsystem. Return code 8.],
    [#cmd("UFSD091E APF SETUP FAILED RC=n (STEPLIB NOT APF AUTHORIZED?)")], [UFSD could not become authorised. The library in the STEPLIB is not on the APF list, and the SVC of RAKF is not there. Return code 8. See the chapter "Installing UFSD" of the _UFSD User's Guide_.],
    [#cmd("UFSD092E CANNOT ALLOCATE CSA ANCHOR")], [The anchor could not be allocated in the common storage. Return code 8.],
    [#cmd("UFSD093E CANNOT ALLOCATE SSVT")], [No storage for the subsystem vector table. Return code 8.],
    [#cmd("UFSD094E CANNOT ALLOCATE SSCT")], [No storage for the subsystem communication table entry. Return code 8.],
    [#cmd("UFSD095I SSCT DEREGISTERED")], [A stop removed the subsystem entry.],
    [#cmd("UFSD096E RECOVERY: STC BLOCK NOT FOUND -- NOTHING QUIESCED, CSA RETAINED")], [After an abend, the recovery routine did not find the control block of the server. Nothing was shut down and the storage is kept. The next start frees it.],
    [#cmd("UFSD096I CSA FREED")], [A stop freed the common storage.],
    [#cmd("UFSD097E CANNOT ENTER SUPERVISOR STATE -- CSA RETAINED, RECLAIMED AT NEXT START")], [A stop could not enter supervisor state. The storage is kept and the anchor stays flagged active. The next start reclaims it if the address space of the server is gone, and refuses only with UFSD155W or UFSD156W; UFSDCLNP then frees it.],
    [#cmd("UFSD097W ABEND SHUTDOWN -- CSA RETAINED, RECLAIMED AT NEXT START")], [The last message after an abend. The storage is kept on purpose and the next start frees it.],
    [#cmd("UFSD098E UFSD ABEND INTERCEPTED -- EMERGENCY SHUTDOWN")], [The first message after an abend. UFSD writes back what it can and stops.],
    [#cmd("UFSD098I UFSD SHUTTING DOWN")], [A stop begins (P UFSD or SHUTDOWN). It is not issued after an abend.],
    [#cmd("UFSD098W n CLIENT(S) STILL IN FLIGHT -- CSA RETAINED, RECLAIMED AT NEXT START")], [Clients were still in the router when the time of about 10 seconds ran out. The storage is kept and the next start frees it. UFSD099I follows.],
    [#cmd("UFSD099I UFSD SHUTDOWN COMPLETE")], [The stop is over. It is not issued after an abend or after UFSD097E.],
  )
]


=== The Configuration Member (100 to 106) <apx-messages-cfg>

These messages come from the reading of the member that the DD statement #cmd("UFSDPRM") names. A statement that is refused is skipped and the rest of the member is read. A read error and a missing ROOT end the start.

#tab(caption: [Messages 100 to 106])[
  #table(columns: (2.9in, 1fr),
    [Message], [Meaning and action],
    [#cmd("UFSD100W CANNOT OPEN DD:UFSDPRM -- USING DEFAULTS")], [The DD statement #cmd("UFSDPRM") is missing or the member cannot be opened. Despite the text, UFSD does not use defaults: UFSD061E follows and the step ends with return code 8.],
    [#cmd("UFSD101W PARMLIB: TOO MANY MOUNT STATEMENTS (MAX n)")], [The member has more than 16 MOUNT statements. The ones after the sixteenth are ignored.],
    [#cmd("UFSD102W PARMLIB: MOUNT MISSING DSN()")], [A MOUNT statement has no DSN(). It is skipped.],
    [#cmd("UFSD103W PARMLIB: MOUNT MISSING PATH()")], [A MOUNT statement has no PATH(). It is skipped.],
    [#cmd("UFSD104W PARMLIB: UNRECOGNIZED STATEMENT: text")], [The line begins with something other than ROOT or MOUNT in upper case. The first 40 characters are shown. The line is ignored.],
    [#cmd("UFSD105W PARMLIB: ROOT STATEMENT MISSING")], [The member has no ROOT statement with a DSN(). UFSD061E follows and the step ends with return code 8.],
    [#cmd("UFSD106E PARMLIB: READ ERROR -- CONFIGURATION INCOMPLETE")], [An input/output error while reading the member. UFSD does not mount a truncated list: UFSD061E follows and the step ends with return code 8.],
  )
]


=== Mount and Unmount Problems (120 to 133) <apx-messages-mnterr>

#tab(caption: [Messages 120 to 133])[
  #table(columns: (2.9in, 1fr),
    [Message], [Meaning and action],
    [#cmd("UFSD120E DYNALLOC FAILED FOR DSN=dsn (S99ERR=xxxx: text)")], [The data set could not be allocated. The code is the error code of the dynamic allocation in hexadecimal, and the text explains it. A data set that does not exist or is not cataloged gives this message. The mount fails.],
    [#cmd("UFSD120E DYNALLOC FAILED FOR DSN=dsn (S99ERR=xxxx)")], [The same, when there is no text for the code.],
    [#cmd("UFSD120E DYNALLOC FAILED FOR DSN=dsn (TEXT UNIT BUILD ERROR)")], [The request for the allocation could not be built, for example because the data set name is empty. The mount fails.],
    [#cmd("UFSD122W CANNOT CREATE MOUNT POINT path (RC=n)")], [UFSD could not create the directory of a mount point on the file system above. The mount is tried all the same.],
    [#cmd("UFSD123W CANNOT MOUNT DSN=dsn ON path")], [A mount statement of the member failed. The message before it says why. The start goes on without it.],
    [#cmd("UFSD124E SUPERBLOCK READ/VALIDATION FAILED FOR DSN=dsn")], [The superblock could not be read or is not valid (UFSD062E or UFSD063E before it). The data set is released and the mount fails.],
    [#cmd("UFSD125W dsn ROOT OWNER=owner, MOUNTED OWNER=owner")], [The disk was formatted for one user ID and mounted with OWNER() naming another. The mount goes on, and OWNER() of the mount decides who may write.],
    [#cmd("UFSD126W MOUNT POINT path NOT FOUND ON PARENT DISK")], [UFSD did not find the mount point on the file system above. The file system is mounted all the same; what is lost is the link in listings of the parent directory.],
    [#cmd("UFSD130W SUPERBLOCK WRITEBACK FAILED FOR DSN=dsn")], [The superblock of a read/write file system could not be written at a stop or an unmount. Treat the file system as damaged and check it before you use it again.],
    [#cmd("UFSD131I SUPERBLOCK WRITTEN FOR DSN=dsn")], [The superblock was written back, at a stop or at an unmount.],
    [#cmd("UFSD132E UNMOUNT: CANNOT UNMOUNT ROOT FILESYSTEM")], [UNMOUNT named the root.],
    [#cmd("UFSD133E UNMOUNT: NO FILESYSTEM MOUNTED ON path")], [UNMOUNT named a path with nothing mounted on it.],
  )
]


=== Cleanup and Reclaim (140 to 157) <apx-messages-clnp>

The messages 143 to 148 and 152 to 156 come from the reclaim routine. They appear at a start that finds the storage of an earlier run, and in UFSDCLNP. The messages 150 and 151 belong to the request handling.

#tab(caption: [Messages 140 to 157])[
  #table(columns: (2.9in, 1fr),
    [Message], [Meaning and action],
    [#cmd("UFSD140I UFSDCLNP STARTING")], [UFSDCLNP begins.],
    [#cmd("UFSD141E UFSDCLNP: APF SETUP FAILED RC=n")], [UFSDCLNP could not become authorised. Return code 8.],
    [#cmd("UFSD142I UFSDCLNP: SUBSYSTEM name NOT REGISTERED")], [There is no subsystem of that name, so there is nothing to free. Return code 0.],
    [#cmd("UFSD143E RECLAIM: CANNOT ENTER SUPERVISOR STATE")], [The reclaim could not enter supervisor state. At a start UFSD003E follows. UFSDCLNP ends with return code 8.],
    [#cmd("UFSD144I RECLAIM: SSVT FUNCTION POINTER CLEARED")], [The subsystem no longer takes requests.],
    [#cmd("UFSD145I RECLAIM: SSCT DEREGISTERED")], [The subsystem entry is removed.],
    [#cmd("UFSD146I RECLAIM: SSI ROUTER MODULE FREED")], [The router is freed from the common storage.],
    [#cmd("UFSD147I RECLAIM: CSA POOLS FREED")], [The request pool, the buffer pool and the trace table are freed.],
    [#cmd("UFSD148I RECLAIM: ANCHOR FREED")], [The anchor is freed.],
    [#cmd("UFSD149I UFSDCLNP COMPLETE")], [UFSDCLNP freed the storage. Return code 0.],
    [#cmd("UFSD150E CORRUPT REQUEST BLOCK (EYE CATCHER MISMATCH)")], [A request block in the common storage has the wrong eye catcher. UFSD posts the waiting client, increments the error counter of STATS, and does not return the block to the pool.],
    [#cmd("UFSD151E INVALID FUNCTION CODE xxxx")], [A request has a function code that UFSD does not know. The request is refused with an error and the error counter of STATS is incremented. The code is in hexadecimal.],
    [#cmd("UFSD152W RECLAIM: n CLIENT(S) STILL IN FLIGHT AFTER DRAIN -- ASSUMING LEAKED, FREEING")], [The reclaim waited about 12 seconds for clients to leave the router and then freed the storage anyway.],
    [#cmd("UFSD153W RECLAIM: ANCHOR EYE MISMATCH AT address")], [The anchor has the wrong eye catcher. It is not freed. The subsystem entry is removed.],
    [#cmd("UFSD154I RECLAIM: NO ANCHOR (SSCTSUSE=NULL)")], [The subsystem entry points to no anchor. The entry is removed.],
    [#cmd("UFSD155W RECLAIM: UFSD IS ACTIVE IN ASCB address -- NOT RECLAIMED (VERIFY WITH D A,L)")], [The server's address space exists. Nothing is freed. A start then gives UFSD002E, UFSDCLNP gives UFSD157W.],
    [#cmd("UFSD156W RECLAIM: ANCHOR FLAGGED ACTIVE AND THE SERVER ASCB CANNOT BE CHECKED -- NOT RECLAIMED")], [The anchor is flagged active, but the address space of the server cannot be checked: the anchor names none, or the system tables cannot be read. A start does not reclaim and gives UFSD002E. UFSDCLNP reclaims in this case.],
    [#cmd("UFSD157W UFSDCLNP: CLEANUP SUPPRESSED -- STOP UFSD FIRST (/P UFSD)")], [UFSDCLNP found a running server and freed nothing. Return code 8.],
  )
]


== UFSFMT <apx-messages-fmt>

The format utility writes to two places. The report and the progress messages
go to #cmd("SYSPRINT") in mixed case, and the option #cmd("QUIET") suppresses
them. Warnings and errors go to #cmd("SYSTERM") in upper case, and
#cmd("QUIET") never suppresses them. Three information messages, #cmd("UFSFMT06I"),
#cmd("UFSFMT07I") and #cmd("UFSFMT08I"), describe a file system that was found, and
they go to #cmd("SYSTERM"), too; the first two are in mixed case. The messages
marked as VERBOSE are issued only with that keyword.

The return code of the utility is 0 when the data set was formatted, 4 after
#cmd("HELP"), and 8 after any error.


=== Control Statements (UFSFMT01 to UFSFMT19) <apx-messages-fmt-parm>

These messages report on the statements in SYSIN. The errors go to SYSTERM. After an error in a statement, UFSFMT reads the rest, reports every error it finds, and ends with UFSFMT15E and return code 8 without touching the data set.

#tab(caption: [Messages UFSFMT01 to UFSFMT19])[
  #table(columns: (2.9in, 0.9in, 1fr),
    [Message], [Written to], [Meaning and action],
    [#cmd("UFSFMT01E BLKSIZE VALUE \"text\" IS NOT NUMERIC")], [#cmd("SYSTERM")], [The value of BLKSIZE has a character that is not a digit.],
    [#cmd("UFSFMT01E BLKSIZE n MUST BE 512 TO 8192 AND A MULTIPLE OF 512")], [#cmd("SYSTERM")], [The block size is out of range or not a multiple of 512.],
    [#cmd("UFSFMT02E INODES VALUE \"text\" IS NOT NUMERIC")], [#cmd("SYSTERM")], [The value of INODES is not a number (digits with at most one decimal point).],
    [#cmd("UFSFMT02E INODES n.n MUST BE 1.0 TO 50.0")], [#cmd("SYSTERM")], [The percentage is out of range.],
    [#cmd("UFSFMT03E DDNAME \"text\" IS NOT A VALID DD NAME")], [#cmd("SYSTERM")], [The value of DDNAME is not a DD name.],
    [#cmd("UFSFMT04E OWNER \"text\" IS NOT A VALID NAME")], [#cmd("SYSTERM")], [The value of OWNER is not a valid name of 1 to 8 characters.],
    [#cmd("UFSFMT04E GROUP \"text\" IS NOT A VALID NAME")], [#cmd("SYSTERM")], [The value of GROUP is not a valid name of 1 to 8 characters.],
    [#cmd("UFSFMT05E DATASET ALREADY CONTAINS A UFS370 FILESYSTEM")], [#cmd("SYSTERM")], [Block 0 of the data set holds a file system and FORCE was not given. UFSFMT06I, UFSFMT07I and UFSFMT08I follow. Return code 8, nothing was written.],
    [#cmd("UFSFMT05W OVERWRITING AN EXISTING UFS370 FILESYSTEM (FORCE SPECIFIED)")], [#cmd("SYSTERM")], [The same situation with FORCE. UFSFMT goes on and overwrites the file system. UFSFMT06I and UFSFMT07I follow.],
    [#cmd("UFSFMT06I   Volume size . . . n blocks (n.nn MB), block size n")], [#cmd("SYSTERM")], [The size of the file system that was found. Issued with UFSFMT05E or UFSFMT05W, in mixed case, and on SYSTERM although it is an information.],
    [#cmd("UFSFMT07I   Created . . . . . yyyy-mm-dd hh:mm:ss")], [#cmd("SYSTERM")], [When the file system that was found was created. As UFSFMT06I. Not issued if the boot block has no creation time.],
    [#cmd("UFSFMT08I SPECIFY FORCE TO OVERWRITE")], [#cmd("SYSTERM")], [Last line of the refusal that begins with UFSFMT05E.],
    [#cmd("UFSFMT09W PARM= IS NOT SUPPORTED -- CONTROL STATEMENTS ARE READ FROM SYSIN")], [#cmd("SYSTERM")], [The EXEC statement has a PARM. It is ignored.],
    [#cmd("UFSFMT10I UFSFMT version -- UFS370 disk format utility")], [#cmd("SYSPRINT")], [The banner. It is issued after SYSIN was read, also when HELP was given or SYSIN had errors. QUIET suppresses it. The version is written as built, not in upper case.],
    [#cmd("UFSFMT11E UNKNOWN KEYWORD \"text\"")], [#cmd("SYSTERM")], [A word in SYSIN is not a keyword and no keyword is waiting for its value.],
    [#cmd("UFSFMT12E KEYWORD \"text\" SPECIFIED MORE THAN ONCE")], [#cmd("SYSTERM")], [A keyword appears twice. The value that follows a repeated keyword is skipped.],
    [#cmd("UFSFMT13E KEYWORD AT END OF SYSIN HAS NO VALUE")], [#cmd("SYSTERM")], [The last word of SYSIN is BLKSIZE, INODES, DDNAME, OWNER or GROUP without its value.],
    [#cmd("UFSFMT14W NO SYSIN -- ALL DEFAULTS APPLY")], [#cmd("SYSTERM")], [There is no SYSIN DD statement. The defaults are used.],
    [#cmd("UFSFMT15E PARAMETER ERRORS -- DATASET NOT FORMATTED")], [#cmd("SYSTERM")], [One or more of the messages 01 to 04, 11, 12, 13 or 17 were issued. Return code 8.],
    [#cmd("UFSFMT16I DDNAME=name BLKSIZE=n INODES=n.n OWNER=name GROUP=name[ FORCE]")], [#cmd("SYSPRINT")], [With VERBOSE: the parameters in effect. OWNER and GROUP read (none) when not given.],
    [#cmd("UFSFMT17E SYSIN READ ERROR -- CONTROL STATEMENTS ARE INCOMPLETE")], [#cmd("SYSTERM")], [An input/output error while reading SYSIN. Return code 8.],
    [#cmd("UFSFMT19I PARAMETERS ACCEPTED")], [#cmd("SYSPRINT")], [With VERBOSE: the statements were accepted.],
  )
]


=== Initializing the Data Set (UFSFMT20 to UFSFMT39) <apx-messages-fmt-fill>

UFSFMT first writes zero blocks into the primary extent until the extent is full. The size of the file system is the number of blocks that this wrote.

#tab(caption: [Messages UFSFMT20 to UFSFMT39])[
  #table(columns: (2.9in, 0.9in, 1fr),
    [Message], [Written to], [Meaning and action],
    [#cmd("UFSFMT20I INITIALIZING DD name")], [#cmd("SYSPRINT")], [With VERBOSE: the fill begins.],
    [#cmd("UFSFMT21E CANNOT ALLOCATE BSAM DCB FOR DD name")], [#cmd("SYSTERM")], [No storage for the data control block. Return code 8.],
    [#cmd("UFSFMT22E CANNOT OPEN DD name FOR READ")], [#cmd("SYSTERM")], [The data set cannot be opened for reading. Return code 8.],
    [#cmd("UFSFMT22E CANNOT OPEN DD name FOR OUTPUT")], [#cmd("SYSTERM")], [The data set cannot be opened for output. Return code 8.],
    [#cmd("UFSFMT23E CANNOT ALLOCATE n BYTE BUFFER")], [#cmd("SYSTERM")], [No storage for the block of zeros. Return code 8.],
    [#cmd("UFSFMT24E DD name DID NOT REACH END OF EXTENT AFTER n BLOCKS")], [#cmd("SYSTERM")], [UFSFMT wrote 1048576 blocks and the extent did not end. The count is a safeguard, not a limit of the file system; an extent of that size or larger is refused. Return code 8.],
    [#cmd("UFSFMT24E NO BLOCKS COULD BE WRITTEN TO DD name")], [#cmd("SYSTERM")], [The first write already met the end of the extent. Return code 8.],
    [#cmd("UFSFMT25E DD name IS ALLOCATED DISP=SHR -- DISP=OLD IS REQUIRED")], [#cmd("SYSTERM")], [The DD statement has DISP=SHR. Nothing was written. Return code 8.],
    [#cmd("UFSFMT26I Initialized n blocks (n.nn MB)")], [#cmd("SYSPRINT")], [The fill is over. The count is the size of the file system. QUIET suppresses it.],
    [#cmd("UFSFMT27E DATASET TOO SMALL: n BLOCKS OF n BYTES CANNOT HOLD A FILESYSTEM")], [#cmd("SYSTERM")], [The extent holds fewer than 8 blocks. Return code 8.],
    [#cmd("UFSFMT28E DD name WRITE FAILED AFTER n BLOCKS, ABEND xxx")], [#cmd("SYSTERM")], [A write failed with an abend other than B37, D37 or E37 (those mean the end of the extent and end the fill). The code is the system completion code. Return code 8.],
    [#cmd("UFSFMT39I INITIALIZE ENDED WITH RC=n")], [#cmd("SYSPRINT")], [With VERBOSE: the return code of the fill.],
  )
]


=== Writing the File System (UFSFMT40 to UFSFMT79) <apx-messages-fmt-write>

#tab(caption: [Messages UFSFMT40 to UFSFMT79])[
  #table(columns: (2.9in, 0.9in, 1fr),
    [Message], [Written to], [Meaning and action],
    [#cmd("UFSFMT40I FORMATTING DD name AS A FILESYSTEM")], [#cmd("SYSPRINT")], [With VERBOSE: the second phase begins.],
    [#cmd("UFSFMT41E CANNOT ALLOCATE BDAM DCB FOR DD name")], [#cmd("SYSTERM")], [No storage for the data control block. Return code 8.],
    [#cmd("UFSFMT42E CANNOT OPEN DD name FOR UPDATE")], [#cmd("SYSTERM")], [The data set cannot be opened for BDAM update. Return code 8.],
    [#cmd("UFSFMT43E CANNOT ALLOCATE n BYTE BUFFER")], [#cmd("SYSTERM")], [No storage for a block buffer. Return code 8.],
    [#cmd("UFSFMT44E WRITE OF BOOT BLOCK FAILED")], [#cmd("SYSTERM")], [Return code 8. Block 0 was not written, so the data set is not a file system and a new run needs no FORCE.],
    [#cmd("UFSFMT45E WRITE OF INODE BLOCK n FAILED")], [#cmd("SYSTERM")], [An inode block could not be written. The number is the sector. Return code 8.],
    [#cmd("UFSFMT46E WRITE OF FREE CHAIN BLOCK n FAILED")], [#cmd("SYSTERM")], [A block of the chain of free blocks could not be written. Return code 8.],
    [#cmd("UFSFMT47W FREE BLOCK CHAIN COVERS n BLOCKS, EXPECTED n")], [#cmd("SYSTERM")], [The free list does not cover as many blocks as the geometry says. The format goes on. Report it.],
    [#cmd("UFSFMT48E GEOMETRY REJECTED (RC=n)")], [#cmd("SYSTERM")], [UFSFMT could not work out a layout for these values. Return code 8.],
    [#cmd("UFSFMT49E WRITE OF SUPERBLOCK FAILED")], [#cmd("SYSTERM")], [Return code 8.],
    [#cmd("UFSFMT51I Formatted n index blocks, n inode slots")], [#cmd("SYSPRINT")], [The inode list is written.],
    [#cmd("UFSFMT52I Formatted n data blocks")], [#cmd("SYSPRINT")], [The free data blocks are written.],
    [#cmd("UFSFMT59I FORMAT ENDED WITH RC=n")], [#cmd("SYSPRINT")], [With VERBOSE: the return code of this phase.],
    [#cmd("UFSFMT60I CREATING ROOT DIRECTORY")], [#cmd("SYSPRINT")], [With VERBOSE: the root directory begins.],
    [#cmd("UFSFMT61E READ OF INODE BLOCK n FAILED")], [#cmd("SYSTERM")], [The first inode block could not be read back. Return code 8.],
    [#cmd("UFSFMT62E WRITE OF ROOT INODE FAILED")], [#cmd("SYSTERM")], [Return code 8.],
    [#cmd("UFSFMT63E WRITE OF ROOT DIRECTORY BLOCK n FAILED")], [#cmd("SYSTERM")], [Return code 8.],
    [#cmd("UFSFMT65I ROOT INODE n, DATA BLOCK n")], [#cmd("SYSPRINT")], [With VERBOSE: where the root directory is.],
    [#cmd("UFSFMT71I Root directory created, owner=name, group=name, mode=0755")], [#cmd("SYSPRINT")], [The root directory exists. The owner and the group read (none) when not given.],
    [#cmd("UFSFMT79I ROOT DIRECTORY ENDED WITH RC=n")], [#cmd("SYSPRINT")], [With VERBOSE: the return code of this phase.],
  )
]


=== The Report (UFSFMT80 to UFSFMT99) <apx-messages-fmt-report>

The report is written to SYSPRINT after a successful format, in mixed case. QUIET suppresses the whole report.

#tab(caption: [Messages UFSFMT80 to UFSFMT99])[
  #table(columns: (2.9in, 0.9in, 1fr),
    [Message], [Written to], [Meaning and action],
    [#cmd("UFSFMT80I Format summary")], [#cmd("SYSPRINT")], [The heading.],
    [#cmd("UFSFMT81I   Dataset . . . . . dsn")], [#cmd("SYSPRINT")], [The data set name from the DD statement. If there is none, the DD name.],
    [#cmd("UFSFMT82I   Block size  . . . n")], [#cmd("SYSPRINT")], [The block size.],
    [#cmd("UFSFMT83I   Total blocks  . . n (n.nn MB)")], [#cmd("SYSPRINT")], [The size of the file system.],
    [#cmd("UFSFMT84I   Inode blocks  . . n (n slots, n free)")], [#cmd("SYSPRINT")], [The inode blocks, the inodes, and the inodes still free.],
    [#cmd("UFSFMT85I   Data blocks . . . n (n free)")], [#cmd("SYSPRINT")], [The data blocks and the ones still free. The root directory takes one.],
    [#cmd("UFSFMT86I   Root owner  . . . owner/group")], [#cmd("SYSPRINT")], [The owner and the group of the root directory; each reads (none) when not given.],
    [#cmd("UFSFMT90I Add to your UFSD parmlib member:")], [#cmd("SYSPRINT")], [Introduces the next line.],
    [#cmd("UFSFMT91I   MOUNT    DSN(dsn) PATH(/your/mount/point) MODE(RW)[ OWNER(owner)]")], [#cmd("SYSPRINT")], [A MOUNT statement for the configuration member. Replace the path. OWNER() is there only when the disk was formatted with an OWNER.],
    [#cmd("UFSFMT99I UFSFMT ENDING WITH RC=0")], [#cmd("SYSPRINT")], [With VERBOSE: the last message of a successful run.],
  )
]
