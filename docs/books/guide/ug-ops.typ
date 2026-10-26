#import "../bookmaster/bookmaster.typ": *

= Running UFSD <ug-ops>

#idx("operation")
This chapter is for the operator. It shows how to start and stop UFSD, what
a healthy start and a normal stop look like on the console, and how to look
at a running server and change its mounts. The complete list of commands and
messages is in the _UFSD Reference_.

== Starting <ug-ops-start>

#idx("start")
Start UFSD with the console command #cmd("S"), as in @ug-install-start:

#screen(```
S UFSD
S UFSD,M=UFSDPRM1
S UFSD,D='SYS1.PARMLIB'
```)

At the start, UFSD checks and sets up its surroundings, and then it reads
the configuration. A good start looks like this on the console. The example
is a system with three file systems:

#screen(raw(read("../ex/ug-ops/start.txt")))

The messages come in the order of the table. The ones in the table that do
not appear above appear only in some cases.

#tab(caption: [The messages of a start, in order])[
  #table(columns: (1.4in, 1fr),
    [Message], [What it tells],
    [#cmd("UFSD000I")], [The version and the commit of the build.],
    [#cmd("UFSD005I")], [The version of LIBC/370 that UFSD was linked with.],
    [#cmd("UFSD006W")], [Only in a build from a changed working tree.],
    [#cmd("UFSD007I")], [How UFSD got its authority: by library (key 0) or by
      SVC (key 8) (@ug-install-auth).],
    [#cmd("UFSD004I")], [Only after a failure: UFSD found the common storage of
      an earlier run and freed it (@ug-recover).],
    [#cmd("UFSD030I") to #cmd("UFSD033I")], [The common storage: the anchor, the
      request pool, the buffer pool and the trace table.],
    [#cmd("UFSD034I")], [The subsystem #cmd("UFS1") is registered.],
    [#cmd("UFSD035I")], [The SSI router is loaded into the common storage.],
    [#cmd("UFSD045I"), #cmd("UFSD047I")], [The session table and the table of
      open files.],
    [#cmd("UFSD060I")], [One line for each file system mounted.],
    [#cmd("UFSD040I")], [How many file systems are mounted.],
    [#cmd("UFSD001I")], [UFSD is ready for clients.],
  )
]

UFSD is ready only when #cmd("UFSD001I") has appeared. Whatever stops the
start before it ends the step with return code 8, after a message that says
what failed. @ug-config-failure and @ug-install-trouble list the common
ones.

If the earlier run did not end cleanly, the start frees what it left behind
before it goes on (@ug-recover-auto). If that earlier server still runs, the
new one refuses to start with #cmd("UFSD002E").

== Stopping <ug-ops-stop>

#idx("stop")
#screen(```
P UFSD
```)

#cmd("F UFSD,SHUTDOWN") does the same. UFSD stops accepting requests, waits
up to about 10 seconds for the requests in progress, and writes the
superblock of every file system back, except those that are mounted with
#cmd("MODE(RO)"). The root is written, too, although #cmd("MOUNT LIST") shows
it as #cmd("RO"). On the console this shows as:

#screen(raw(read("../ex/ug-ops/stop.txt")))

The line #cmd("UFSD131I") appears once for each file system that is written back. If a
superblock cannot be written, there is #cmd("UFSD130W SUPERBLOCK WRITEBACK FAILED FOR DSN=...") in its place. Treat that as a damaged file system and
check it before the next start.

A clean stop frees all common storage. If clients were still at work when the
time of about 10 seconds ran out, UFSD says #cmd("UFSD098W n CLIENT(S) STILL IN FLIGHT -- CSA RETAINED, RECLAIMED AT NEXT START"), keeps the storage, and ends with
#cmd("UFSD099I"). The next start frees it.

Stop the programs that use UFSD, such as HTTPD or FTPD, before you stop
UFSD. A request that is still in progress when the time runs out is not
completed.

== The Operator Commands <ug-ops-commands>

#idx("MODIFY")
All commands go through the MODIFY command, #cmd("F UFSD,command"). UFSD
reads at most 63 characters of the command, folds them to upper case and
ignores trailing blanks. The answers are console messages.

#tab(caption: [The operator commands])[
  #table(columns: (3in, 1fr),
    [Command], [What it does],
    [#cmd("STATS")], [Counters, free pools, the mounted file systems.],
    [#cmd("SESSIONS")], [The active client sessions.],
    [#cmd("SESSIONS PRUNE")], [Releases the sessions of address spaces that no
      longer exist.],
    [#cmd("MOUNT LIST")], [The mounted file systems.],
    [#cmd("MOUNT DSN=dsn,PATH=/path[,MODE=RW][,OWNER=user]")], [Mounts a data set.],
    [#cmd("UNMOUNT PATH=/path")], [Unmounts the file system at that path.],
    [#cmd("TRACE ON"), #cmd("TRACE OFF"), #cmd("TRACE DUMP")], [Switches
      the request trace and prints the trace table.],
    [#cmd("REBUILD")], [Rebuilds the free-block and free-inode caches of the
      read/write file systems.],
    [#cmd("HELP")], [Lists the commands.],
    [#cmd("SHUTDOWN")], [Stops UFSD, like #cmd("P UFSD").],
  )
]

A command that UFSD does not know, or a bare #cmd("MOUNT"), #cmd("UNMOUNT") or
#cmd("TRACE") without its operand, gets #cmd("UFSD021E UNKNOWN COMMAND: ...")
followed by the list of commands (#cmd("UFSD020I")).

=== Looking at the Server <ug-ops-stats>

#idx("STATS")
#cmd("F UFSD,STATS") shows the state of the server:

#screen(raw(read("../ex/ug-ops/stats.txt")))

Note that #cmd("UFSD017I") comes before #cmd("UFSD016I"). The line
#cmd("UFSD018I") appears once for each file system. After #cmd("RW") comes the
owner of the mount, if there is one, in parentheses.

Each of #cmd("FREEBLK") and #cmd("FREEINODE") has two numbers. The first is the
number of entries in the cache that UFSD keeps in the superblock (at most 51
blocks and 64 inodes), and the second is the total number of free blocks or
inodes of the file system. Read the free space from the second number. In the
example the file system on #cmd("/tmp") has 1275 free blocks, of which 51 are in
the cache. The first number goes up and down as UFSD uses and refills the
cache, and it is no reason for concern.

The numbers to watch are the free requests and buffers (they should return
to their maximum when no client is active), #cmd("IN FLIGHT") (the requests
being served at this moment), and the free blocks and inodes of each file
system. When the free blocks or the free inodes run out, clients get write
errors, and the only cure is a larger disk (@ug-disks). The counter
#cmd("ERRORS") counts hard errors only: input/output errors, no space, no
free inodes, a damaged request, an unknown function and an invalid session or
argument. A file that does not exist, a name that exists already and a
refused write on a read only or foreign file system are normal answers
and are not counted.

#cmd("F UFSD,SESSIONS") shows who is connected:

#screen(raw(read("../ex/ug-ops/sessions.txt")))

A session belongs to one client program. #cmd("FDS") is the number of open
files and the most it may have. The user ID in #cmd("OWNER") is the one that
the write check of a mount compares (@ug-config-access). If a client ended
without closing its session, the session stays in the list. #cmd("SESSIONS PRUNE")
looks for the address spaces that no longer exist and releases their
sessions, with #cmd("UFSD052I") for each and #cmd("UFSD053I") for the total.

=== Mounting and Unmounting <ug-ops-mount>

#idx("mount", "dynamic")
#idx("MOUNT LIST")
#cmd("MOUNT LIST") shows the table of the mounted file systems, with the mode
and the owner. Use it to check what UFSD understood of the configuration:

#screen(raw(read("../ex/ug-ops/mount-list.txt")))

To mount a file system while UFSD runs, give the data set and the path:

#screen(```
F UFSD,MOUNT DSN=PROJECT.DATA,PATH=/proj,MODE=RW
F UFSD,MOUNT DSN=IBMUSER.UFSHOME,PATH=/u/ibmuser,MODE=RW,OWNER=IBMUSER
```)

The answer is #cmd("UFSD060I MOUNTED ... ON ... (RW)"). Four rules apply:

- The path must be absolute (#cmd("UFSD065E")), and nothing may be mounted
  on it already (#cmd("UFSD067E")).
- The path is folded to upper case, because the console does that to the
  whole command. A mount point such as #cmd("/u/ibmuser") made from the
  console is therefore #cmd("/U/IBMUSER"). Use the configuration member for
  paths with lower case letters.
- The mode is read/write only if the value begins with #cmd("RW"). Anything
  else is read only.
- The console command does not create the directory of the mount point. If
  the directory is not there, the mount is made all the same, with the warning
  #cmd("UFSD126W MOUNT POINT path NOT FOUND ON PARENT DISK"). A mount made from
  the member creates the directory (@ug-config-points).

A mount made this way is not saved. It is gone when UFSD stops. To keep it,
add it to the member.

#note[UFSD does not check whether the data set is mounted already. It compares
only the path, and #cmd("/tmp") and #cmd("/TMP") are two paths. The example
mounts a data set a second time on the upper case form of its path:]

#screen(raw(read("../ex/ug-ops/mount-dup.txt")))

The data set is now open twice for writing, with two copies of its superblock
in storage, and each copy is written back. Run #cmd("MOUNT LIST") before you
mount, and never mount a read/write data set twice.

#cmd("UNMOUNT PATH=/path") (or just #cmd("UNMOUNT /path")) writes the
superblock back, closes the data set and frees it. The answer is
#cmd("UFSD064I UNMOUNTING dsn FROM path"), and for a read/write file system
#cmd("UFSD131I"):

#screen(raw(read("../ex/ug-ops/unmount.txt")))

The root
cannot be unmounted (#cmd("UFSD132E")), and a path with nothing mounted on it
gets #cmd("UFSD133E"). Unmount a file system only when no client has a file
open on it.

=== The Trace <ug-ops-trace>

#idx("TRACE")
UFSD can keep a trace of its requests in a table of 256 entries in the common
storage. #cmd("TRACE ON") switches it on and
#cmd("TRACE OFF") switches it off, each answered by #cmd("UFSD083I TRACE ENABLED")
or #cmd("UFSD083I TRACE DISABLED"). The recording is on from the start of UFSD.
#cmd("TRACE DUMP") prints the table, oldest first, as
#cmd("UFSD080I TRACE DUMP: n ENTRies") and a #cmd("UFSD081I") line for each entry:
a number, the time stamp (#cmd("T")), the kind of event (#cmd("F")), a code (#cmd("RC")) and the session
(#cmd("SESS")), all in hexadecimal. The Reference lists the events.

#screen(raw(read("../ex/ug-ops/trace-dump.txt")))

The table is for the
support of the product. When you report a problem, say that you have it.

=== Rebuilding the Free Lists <ug-ops-rebuild>

#idx("REBUILD")
UFSD keeps a cache of free blocks and free inodes for each file system and
refills it by a scan when it runs empty (#cmd("UFSD070I") and #cmd("UFSD076I")
during a write). #cmd("REBUILD") scans every read/write file system at once and
fills the caches. It skips the file systems that are mounted with
#cmd("MODE(RO)") (#cmd("UFSD072I")). The root is scanned although
#cmd("MOUNT LIST") shows it as #cmd("RO"). Each
file system gets #cmd("UFSD073I REBUILD: SCANNING ...") and then
#cmd("UFSD074I REBUILD: ... DONE, FREEBLK=n FREEINODE=n") or
#cmd("UFSD075E ... FAILED"). Because UFSD does one thing at a time, clients wait
until the scan is over. On a large disk that can take some time, so choose a
quiet moment. #cmd("REBUILD") does not repair files that were stored wrongly
before, and it makes no promise about the state of the free lists afterwards.

In the example, a further file system was mounted with #cmd("MODE=RO"). The
names #cmd("UFD00001") and so on are the DD names that UFSD gave the data sets
when it allocated them. The number counts up with each mount, also after an
unmount, so it does not tell the place of the file system in the table.

#screen(raw(read("../ex/ug-ops/rebuild.txt")))

A line such as #cmd("FOUND 124 INODES (CACHE REFILLED TO 64)") shows the total
number found and the size of the cache after the refill. The cache holds at
most 51 blocks and 64 inodes.

== Things to Know <ug-ops-know>

- UFSD handles one request at a time. A slow disk delays all clients.
- UFSD writes the superblock of a read/write file system back at the stop
  and at an unmount. Use #cmd("P UFSD") to stop it, and cancel it only when
  it does not react (@ug-recover).
- The console messages are plain WTO messages with no routing code of their
  own, and the text of one is at most about 124 characters.
