#import "../bookmaster/bookmaster.typ": *

= The Started Task and Its Utilities <ref-stc>

#idx("started task")
This chapter describes the started task UFSD (ufsd) as MVS sees it: its
modules, its procedure, how it gets its authority, the order of its console
messages at the start and at the end, and its return codes. The last section
describes the cleanup utility UFSDCLNP. The format utility UFSFMT has a chapter
of its own (@ref-fmt).

== The Load Modules <ref-stc-modules>

#idx("load modules")
The install job puts four load modules into #cmd("UFSD.LINKLIB"):

#tab(caption: [The load modules of UFSD 1.5.0])[
  #table(columns: (1.1in, 0.7in, 1fr),
    [Module], [AC], [Use],
    [#cmd("UFSD")], [1], [The server. It runs as the started task #cmd("UFSD").],
    [#cmd("UFSDSSIR")], [1], [The router for the subsystem interface. It is
      not a job step. UFSD loads it into the common storage (CSA) at the start
      and frees it at the end. Clients reach it through #cmd("IEFSSREQ").],
    [#cmd("UFSDCLNP")], [1], [The cleanup utility (@ref-stc-clnp).],
    [#cmd("UFSFMT")], [0], [The format utility (@ref-fmt).],
  )
]

The first three modules are reentrant. #cmd("UFSFMT") is serially reusable
only. The client library #cmd("libufs") is not a load module. It is linked into the
programs that use UFSD and is not part of this book.

== The Subsystem <ref-stc-subsys>

#idx("subsystem")
#idx("UFS1")
UFSD registers the subsystem #cmd("UFS1"). The name is compiled into UFSD
and into every client. The registration is done at the start (#cmd("UFSD034I"))
and removed at a clean end (#cmd("UFSD095I")). Clients call the subsystem
through #cmd("IEFSSREQ"); the router in the common storage puts their
requests into a queue that the server works off one at a time.

Only one UFSD can run at a time, because only one subsystem of the name can
be registered. A second start is refused (@ref-stc-start).

== Authority <ref-stc-auth>

#idx("authorization")
#idx("APF")
UFSD and UFSDCLNP work in supervisor state and in storage of key 0, so they
must run authorised. There are two ways:

#tab(caption: [The two ways to authority])[
  #table(columns: (1.4in, 1.6in, 1fr),
    [Way], [Message], [Notes],
    [The SVC 244 of RAKF], [#cmd("UFSD007I AUTHORIZED BY SVC (MODULE KEY 8)")],
      [The module is fetched into storage of key 8, because the step was not
      authorised when MVS loaded the program. TK4- and TK5 have RAKF.],
    [The library is in the APF list], [#cmd("UFSD007I AUTHORIZED BY LIBRARY (MODULE KEY 0)")],
      [The module is fetched into storage of key 0. MVS 3.8j reads the APF
      list at the IPL only.],
  )
]

Without either, the start fails with #cmd("UFSD091E APF SETUP FAILED RC=n
(STEPLIB NOT APF AUTHORIZED?)") and return code 8. UFSDCLNP fails the same way
with #cmd("UFSD141E"). The key in the message is inferred from the way, not
measured. Release 1.5.0 was tested on the RAKF route only.

== The Procedure <ref-stc-proc>

#idx("procedure")
The sample library holds the procedure as #cmd("UFSD.SAMPLIB(UFSD)"):

#screen(```
//UFSD     PROC M=UFSDPRM0,
//            D='SYS2.PARMLIB'
//UFSD     EXEC PGM=UFSD,REGION=4M,TIME=1440
//STEPLIB  DD  DISP=SHR,DSN=UFSD.LINKLIB
//UFSDPRM  DD  DISP=SHR,DSN=&D(&M),FREE=CLOSE
//SYSUDUMP DD  SYSOUT=*
```)

(The comment lines of the member are left out.)

#tab(caption: [The parts of the procedure])[
  #table(columns: (1.2in, 1fr),
    [Part], [Meaning],
    [#cmd("M=")], [The member of the configuration. The default is
      #cmd("UFSDPRM0"). The statements in it are described in @ref-cfg.],
    [#cmd("D=")], [The data set of the member. The default is
      #cmd("SYS2.PARMLIB"). On a system where that data set does not exist, give
      another one, as in #cmd("S UFSD,D='SYS1.PARMLIB'"), or change the default
      in the procedure.],
    [#cmd("REGION=4M")], [The region.],
    [#cmd("TIME=1440")], [The time limit of the step. The value 1440 means that
      there is no limit.],
    [#cmd("STEPLIB")], [The library with the modules. It must be authorised as
      described above if the APF way is used.],
    [#cmd("UFSDPRM")], [The configuration member. It is closed and freed once
      UFSD has read it (#cmd("FREE=CLOSE")). A change of the member is
      therefore seen at the next start only.],
    [#cmd("SYSUDUMP")], [The dump of an abend.],
  )
]

Start and stop the task from the console:

#screen(```
S UFSD
S UFSD,M=UFSDPRM1
S UFSD,D='SYS1.PARMLIB'
F UFSD,command
P UFSD
```)

The member #cmd("UFSD") has to be in a procedure library of the system. The
install job does not copy it there.

== Reading the Console <ref-stc-console>

#idx("console")
UFSD takes the commands #cmd("MODIFY") and #cmd("STOP") through the console
interface of MVS. It accepts the start request and four queued #cmd("MODIFY")
commands at once. A command that arrives while the queue is full is rejected
by MVS itself. The commands are described in @ref-cmd.

All messages of UFSD are written with #cmd("WTO") without a routing code of
their own and have the form #cmd("UFSDnnnx"), where _x_ is I, W or E
(information, warning, error). They are at most about 124 characters long.
The numbers are not ordered by topic, so use @apx-messages to look up a
message.

== Starting <ref-stc-start>

#idx("start", "order of messages")
The messages of a start come in the order of the table. A line in brackets
appears only in some cases. The start ends with return code 8 at any of the
points where an error is listed, and the task is not usable then.

#tab(caption: [The steps of a start])[
  #table(columns: (1.6in, 1fr),
    [Messages], [Step],
    [#cmd("UFSD090E")], [The console interface could not be set up. Return code 8.],
    [#cmd("UFSD091E")], [No authority (@ref-stc-auth). Return code 8.],
    [#cmd("UFSD000I"), #cmd("UFSD005I"), [#cmd("UFSD006W")], #cmd("UFSD007I")],
      [The banner: the version and commit of the build, the version of the C
      runtime, a warning if the build came from a changed working tree, and the
      way to the authority. The recovery from an abend is active from here
      on.],
    [#cmd("UFSD155W"), #cmd("UFSD156W"), #cmd("UFSD143E"), #cmd("UFSD144I") to
      #cmd("UFSD148I"), #cmd("UFSD152W") to #cmd("UFSD154I")],
      [The reclaim of an earlier run (@ref-stc-reclaim). They appear only if an
      anchor of an earlier run exists.],
    [#cmd("UFSD002E"), #cmd("UFSD003E")], [The reclaim found a running server, an anchor
      that cannot be checked, or failed. Return code 8.],
    [[#cmd("UFSD004I")]], [An earlier run was reclaimed.],
    [#cmd("UFSD092E")], [The anchor in the common storage could not be allocated. Return
      code 8.],
    [#cmd("UFSD038E"), #cmd("UFSD039E")], [The common storage could not be set up. Return
      code 8.],
    [#cmd("UFSD030I") to #cmd("UFSD033I")], [The common storage: the anchor and the
      request pool, the buffer pool and the trace table.],
    [#cmd("UFSD034I")], [The subsystem is registered. A failure is one of
      #cmd("UFSD091E") (the second message of this number), #cmd("UFSD093E"),
      #cmd("UFSD094E") or #cmd("UFSD025E"). Return code 8.],
    [#cmd("UFSD035I")], [The router is loaded. A failure is #cmd("UFSD026E") or
      #cmd("UFSD027E"). Return code 8.],
    [#cmd("UFSD045I"), #cmd("UFSD047I")], [The session table and the table of open
      files. A failure is #cmd("UFSD045E") to #cmd("UFSD048E"). Return code 8.],
    [#cmd("UFSD100W") to #cmd("UFSD106E"), #cmd("UFSD061E")], [The configuration
      cannot be read or has no root (@ref-cfg-errors). Return code 8, after the
      end procedure below.],
    [#cmd("UFSD060I"), #cmd("UFSD122W"), #cmd("UFSD123W"), #cmd("UFSD125W"),
      #cmd("UFSD126W")], [Each file system is mounted (@ref-cfg-mount).],
    [#cmd("UFSD040I")], [The number of mounted file systems.],
    [#cmd("UFSD001I")], [UFSD is ready for clients. The text shows the version, the
      number of file systems, the size of the common storage, and the sizes of
      the session table and the table of open files.],
  )
]

If the mount of the root file system or the reading of the configuration
fails, the start runs the normal end procedure of @ref-stc-stop for what it has
set up so far and then ends with return code 8. There are no clients yet,
so this is quick and leaves no common storage behind.

=== Reclaim at the Start <ref-stc-reclaim>

#idx("reclaim")
Before UFSD allocates anything, it looks for the anchor of an earlier run.
The result depends on the state of the anchor and on whether the address space
that owned it still exists. The steps and the table of decisions are in the
chapter on recovery of the _UFSD User's Guide_. In short:

- an anchor whose server is gone is freed (#cmd("UFSD144I") to
  #cmd("UFSD148I")), and #cmd("UFSD004I") follows;
- an anchor of a running server is not touched, and the start ends with
  #cmd("UFSD155W") and #cmd("UFSD002E");
- an anchor that cannot be checked is not touched by the start either
  (#cmd("UFSD156W"), #cmd("UFSD002E")). #cmd("UFSDCLNP") frees it
  (@ref-stc-clnp);
- if clients are still inside the router, the reclaim waits for them for about
  12 seconds, then prints #cmd("UFSD152W") and goes on.

== Stopping <ref-stc-stop>

#idx("stop", "order of messages")
#cmd("P UFSD") and #cmd("F UFSD,SHUTDOWN") end the main loop of the task and
start the end procedure. The stop command gives no message of its own.

=== The Normal End

The order of the steps is: announce, close the door, close the file systems,
wait for the clients, free the storage.

#tab(caption: [The steps of a normal end])[
  #table(columns: (1.6in, 1fr),
    [Messages], [Step],
    [#cmd("UFSD098I")], [The end procedure starts.],
    [], [The subsystem entry is cut off, and the active flag in the anchor is
      cleared. New requests are refused, and clients that wait in the router
      leave at their next time-out. There is no message.],
    [#cmd("UFSD131I") or #cmd("UFSD130W")], [The superblock of each file system
      that is not mounted with #cmd("MODE(RO)") is written back, the root included and the data sets are closed. One message
      for each file system.],
    [#cmd("UFSD098W")], [Only if clients were still in the router after about 10
      seconds. The common storage is kept, and the next step is #cmd("UFSD099I").],
    [#cmd("UFSD095I")], [The subsystem is deregistered.],
    [#cmd("UFSD036I")], [The router is unloaded.],
    [#cmd("UFSD048I")], [The table of open files is freed.],
    [#cmd("UFSD046I")], [The session table is freed.],
    [#cmd("UFSD096I")], [The pools of the common storage are freed.],
    [#cmd("UFSD099I")], [The end procedure is complete.],
  )
]

The task ends with return code 0, also after #cmd("UFSD098W").

=== The Abend Path

#idx("ESTAE")
UFSD has a recovery routine for abends of the task (ESTAE). It runs when the
task abends or is cancelled. In the usual case it writes these messages, in
this order:

#screen(```
UFSD098E UFSD ABEND INTERCEPTED -- EMERGENCY SHUTDOWN
UFSD131I SUPERBLOCK WRITTEN FOR DSN=dsn
UFSD097W ABEND SHUTDOWN -- CSA RETAINED, RECLAIMED AT NEXT START
```)

There is a #cmd("UFSD131I") (or #cmd("UFSD130W")) for each file
system that is written back. Two other cases exist:

- *#cmd("UFSD096E").* The routine did not find the control block of the task.
  Nothing was quiesced, and the storage is kept. No other message follows.
- *#cmd("UFSD097E").* The routine could not enter supervisor state. The
  storage is kept and the active flag stays set. The next start decides by the
  liveness check: it reclaims when the server's address space is gone, and
  refuses only with #cmd("UFSD155W") or #cmd("UFSD156W"). Only after
  #cmd("UFSD156W") does #cmd("UFSDCLNP") work. After #cmd("UFSD155W") it
  refuses too. The message comes before the superblocks are
  written. The same message can also appear at a normal stop. This path was
  read from the source and has not been run.

The routine cuts off the subsystem and clears the active flag, but it frees no
storage, because another address space may be running inside the router or
using the pools. The dump of the abend is taken after the routine. The next
start, or #cmd("UFSDCLNP"), frees what is left. The task has no #cmd("UFSD099I")
on this path.

During the termination of an abended task only the first and the last message
reach the console reliably. The last message is therefore the one that tells
the state of the storage.

== Return Codes <ref-stc-rc>

#idx("return codes", "UFSD")
UFSD ends with return code 0 after a normal stop. Every failure of the start
ends the step with return code 8, after a message that says what failed. The
abend of the task is a system or user completion code and not a return code.

== The Cleanup Utility UFSDCLNP <ref-stc-clnp>

#idx("UFSDCLNP")
#cmd("UFSDCLNP") runs the same reclaim as the start of UFSD (@ref-stc-reclaim)
and nothing else. It frees the common storage of an earlier run without
starting a server. It is a started task or a batch step with one DD statement:

#screen(```
//UFSDCLNP PROC
//CLEANUP  EXEC PGM=UFSDCLNP
//STEPLIB  DD  DISP=SHR,DSN=UFSD.LINKLIB
```)

The library needs the same authority as that of UFSD (@ref-stc-auth). The
program reads no input and takes no parameter. Start the procedure with
#cmd("S UFSDCLNP") or put the step into a job.

#tab(caption: [The messages and return codes of UFSDCLNP])[
  #table(columns: (1.6in, 0.7in, 1fr),
    [Message], [Code], [Meaning],
    [#cmd("UFSD140I")], [], [The program starts.],
    [#cmd("UFSD141E")], [8], [No authority.],
    [#cmd("UFSD142I")], [0], [The subsystem is not registered. There is nothing to
      free.],
    [#cmd("UFSD155W"), #cmd("UFSD157W")], [8], [The server is running. Nothing was freed.],
    [#cmd("UFSD143E")], [8], [The program could not enter supervisor state.],
    [#cmd("UFSD144I") to #cmd("UFSD148I")], [], [The steps of the reclaim.],
    [#cmd("UFSD152W"), #cmd("UFSD153W"), #cmd("UFSD154I")], [], [A count of clients
      that never reached zero; an anchor that is not valid and is not freed (the
      entry of the subsystem is removed); no anchor to free.],
    [#cmd("UFSD149I")], [0], [The cleanup is complete.],
  )
]

If the anchor does not name a live address space, #cmd("UFSDCLNP") reclaims it,
including the case that the anchor carries no address space at all
(#cmd("UFSD156W")). Where #cmd("S UFSD") refuses with #cmd("UFSD156W"), #cmd("UFSDCLNP")
works. After #cmd("UFSD155W") it refuses as well. The
table of decisions is in the _UFSD User's Guide_.
