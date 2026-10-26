#import "../bookmaster/bookmaster.typ": *

= Recovery After a Failure <ug-recover>

#idx("recovery")
This chapter is for the day UFSD ends in a way it did not plan: it abends, the
operator cancels it, it hangs, or the system is left with the common storage
of a server that is gone. In nearly all of these cases the cure is to start
UFSD again. The chapter explains why, what the exceptions are, and what to do
when the start refuses.

== How UFSD Ends <ug-recover-ends>

#idx("shutdown", "abnormal")
UFSD owns storage that does not belong to its address space: the anchor, the
pools and the SSI router are in the common storage (CSA), and the subsystem
#cmd("UFS1") is registered. If they stay behind, clients that call the
router find it gone or half gone. There are three ways to the end:

#tab(caption: [How UFSD ends and what is left behind])[
  #table(columns: (1.6in, 1.4in, 1fr),
    [End], [Common storage], [What to do],
    [#cmd("P UFSD") or #cmd("F UFSD,SHUTDOWN")], [Freed], [Nothing. This is the normal stop (@ug-ops-stop).],
    [A clean stop that found clients still at work], [Kept], [Start UFSD. The start frees it. The console shows
      #cmd("UFSD098W").],
    [An abend, or #cmd("C UFSD")], [Kept], [Start UFSD. The start frees it. The console shows
      #cmd("UFSD098E") and, last, #cmd("UFSD097W").],
  )
]

After an abend or a cancel, UFSD does two things before the dump is taken.
It cuts the subsystem off from new requests and it clears the flag that says
the server is active, so that clients already waiting in the router give up
on their next time-out. It frees nothing else. The reason is that another
address space may still be running inside the router or touching the pools,
and freeing them at that moment would abend that address space. The storage
is therefore freed later, when no one can be using it. This is by design and
not a defect.

On the console, an operator #cmd("C UFSD") shows the abend path like this. The
system completion code of the cancel is S222:

#screen(raw(read("../ex/ug-recover/abend.txt")))

The last message of the abend path says what state the storage is in:

#screen(```
UFSD097W ABEND SHUTDOWN -- CSA RETAINED, RECLAIMED AT NEXT START
```)

Superblocks: the abend path tries to write the superblock of every
file system back, except those mounted with #cmd("MODE(RO)") (#cmd("UFSD131I") for each, or
#cmd("UFSD130W") when one fails). Treat a #cmd("UFSD130W") as a damaged file
system and check it before you use it again.

== Starting Again <ug-recover-auto>

#idx("reclaim")
#idx("orphan")
Start UFSD as usual:

#screen(```
S UFSD
```)

The start looks for the anchor of an earlier run before it registers
anything. If it finds one that no longer has a running server, it frees it
first. This is called a _reclaim_. On the console the reclaim shows as these
messages, between the start banner and the allocation of the new storage:

#screen(raw(read("../ex/ug-recover/reclaim.txt")))

The reclaim reads the anchor of an earlier release as well. The anchor of
the releases 1.4.0 and 1.5.0 is 132 bytes long, that of the releases 1.5.1 and 1.5.2 is
152 bytes (@ug-install-from151), and the reclaim handles either. The screen above
is the start of release 1.5.2 after a cancelled server of release 1.5.0. A
server of release 1.4.0 was reclaimed by the start of release 1.5.1 in an
earlier test; its anchor has the same layout as the one of 1.5.0, which was
checked in the source.

If clients were still waiting in the router when the server died, the reclaim
gives them a short time to leave before it frees the router. The time is
about 12 seconds at most. If a count of clients still stands after that, the
reclaim assumes that they will never leave (an address space that was
cancelled while it was waiting cannot count itself out), prints
#cmd("UFSD152W") and frees the storage anyway. The alternative would be to
leave the storage where it is, and only an IPL could remove it then.

This covers every abnormal end of the server that MVS lets happen, including
#cmd("FORCE"). You do not need to run anything else before the start.

== When the Start Refuses <ug-recover-guard>

#idx("liveness check")
A reclaim must never free the storage of a server that is running. Before it
touches anything, it takes the address of the server's address space control
block (ASCB) from the anchor and looks for it among the address spaces that
exist. The result decides:

#tab(caption: [What the start and UFSDCLNP do with the anchor they find])[
  #table(columns: (2.2in, 1.4in, 1fr),
    [The anchor], [#cmd("S UFSD")], [#cmd("S UFSDCLNP")],
    [Flag _active_ is clear: a stop path ran], [Reclaims], [Reclaims],
    [Flag set, and the ASCB is a live address space], [Refuses, with #cmd("UFSD155W") and
      #cmd("UFSD002E")], [Refuses, with #cmd("UFSD155W") and #cmd("UFSD157W"),
      return code 8],
    [Flag set, and the ASCB belongs to no address space], [Reclaims], [Reclaims],
    [Flag set, and the ASCB is the caller's own], [Reclaims], [Reclaims],
    [Flag set, and there is no ASCB to look at (#cmd("UFSD156W"))], [Refuses,
      with #cmd("UFSD002E")], [Reclaims],
  )
]

Three of these rows need a word.

- *Live server.* This is the case where UFSD is running and someone
  starts a second one, or runs the cleanup program. Nothing is freed. Stop
  the running server first, and do not try to get around the check: two
  servers cannot own one subsystem.
- *The caller's own ASCB.* When an address space ends, its ASCB goes back to
  the system, and the next address space created, usually the restart itself,
  can get the same block. Without this row, a restart would find its
  predecessor's address, believe it belongs to a live server and refuse. Two
  address spaces that exist at the same time never share an ASCB, so the row
  cannot hide a live server.
- *No ASCB to look at.* The anchor does not name an address space, or the
  system tables that list the address spaces cannot be read. A start
  refuses because it cannot be sure. #cmd("UFSDCLNP") reclaims, because this
  is the case it exists for.

The check compares only the address of the ASCB. It reads nothing from the
block, because the storage of an ended address space may be reused. That
leaves one error, and it is the harmless one: if the address has been handed
to a third address space, a server that is really dead looks alive, and the
start refuses. Check with #cmd("D A,L"). If no UFSD is listed, the subsystem
entry is an orphan that the check cannot recognise, and an IPL is the way out.

== The Cleanup Program UFSDCLNP <ug-recover-clnp>

#idx("UFSDCLNP")
#cmd("UFSDCLNP") is a batch program that does the same reclaim as the start
and nothing else. Use it when you want to free the storage without starting a
new server, or when the start refuses with #cmd("UFSD156W") and you know that
no server runs. It is the load module #cmd("UFSDCLNP") of #cmd("UFSD.LINKLIB")
and it needs the same authority as UFSD (@ug-install-auth). The sample
library has a procedure for it, #cmd("UFSD.SAMPLIB(UFSDCLNP)"). Copy it to
the procedure library of the system like the procedure #cmd("UFSD")
(@ug-install-proc) and start it:

#screen(```
S UFSDCLNP
```)

After a cancel it frees what the server left behind:

#screen(raw(read("../ex/ug-recover/clnp.txt")))

The plus sign in front of #cmd("UFSD140I") is not part of the message. When
nothing is registered, the program says so and ends:

#screen(raw(read("../ex/ug-recover/clnp-none.txt")))

Its messages, in the order they come:

#tab(caption: [The messages of UFSDCLNP])[
  #table(columns: (1.4in, 1fr),
    [Message], [What it tells],
    [#cmd("UFSD140I")], [UFSDCLNP starts.],
    [#cmd("UFSD141E")], [UFSDCLNP could not get its authority (the
      message gives the return code of the setup). The usual cause is a
      #cmd("STEPLIB") that is not APF-authorised. Return code 8.],
    [#cmd("UFSD142I UFSDCLNP: SUBSYSTEM UFS1 NOT REGISTERED")], [There is nothing to
      free. Return code 0.],
    [#cmd("UFSD144I") to #cmd("UFSD148I")], [The steps of the reclaim, as above.],
    [#cmd("UFSD152W"), #cmd("UFSD153W"), #cmd("UFSD154I")], [A client count that
      never reached zero; an anchor whose eye catcher is wrong, which is not
      freed (the subsystem entry is removed); no anchor to free.],
    [#cmd("UFSD155W"), #cmd("UFSD157W")], [The server is running. Nothing was
      freed. Return code 8.],
    [#cmd("UFSD149I")], [The cleanup is complete. Return code 0.],
  )
]

It is safe to run when UFSD is not registered. It reports that there is
nothing to do and ends with return code 0.

A reclaim that fails for another reason, for example because the program
could not enter supervisor state (#cmd("UFSD143E")), ends with return code 8
and frees nothing more. Run the program again; if it fails again, an IPL is
the only way left.

== A Server That Does Not React <ug-recover-hung>

#idx("hung server")
A server that is alive but does not answer is not a case for #cmd("UFSDCLNP").
The check sees a live address space and refuses, whether or not the server
still works. Take the address space down first, one step at a time, and look
at the console after each step:

+ #cmd("P UFSD") asks for a normal stop. Wait about 10 seconds for the
  drain, and a little longer if clients are slow to leave.
+ #cmd("C UFSD") cancels it. The abend path runs, and the storage is kept.
+ #cmd("FORCE UFSD") only if the cancel did not end it. MVS refuses a
  #cmd("FORCE") for an address space that can still be cancelled
  (#cmd("IEE838I ... CANCELABLE - ISSUE CANCEL BEFORE FORCE")), so issue
  the cancel first.

After the address space is gone, #cmd("S UFSD") reclaims what is left. Do not
start the cleanup program in between.

If you lose the address space but the start refuses all the same, check
with #cmd("D A,L") (see above). A server that cannot even enter supervisor
state at the stop gives #cmd("UFSD097E"): the storage is retained, and the
active flag stays set. The address space ends after the message, so the
next start finds a flagged anchor whose ASCB belongs to no address space, and
reclaims it like the leftovers of an abend (the table above). It refuses only
when the ASCB cannot be checked (#cmd("UFSD156W")) or has been handed to
another address space (#cmd("UFSD155W")). After #cmd("UFSD156W"),
#cmd("UFSDCLNP") reclaims. After #cmd("UFSD155W") it refuses as well (see the
table above). This path was read from the source and has not been run.

After a restart that followed an abend you can have the caches of free blocks
and free inodes of the read/write file systems rebuilt with #cmd("REBUILD")
(@ug-ops-rebuild). That is a repair of the caches, not of the files, and the
guide has not measured that it is needed after an abend.

== Messages of an Abnormal End <ug-recover-msg>

#tab(caption: [The messages that tell you the storage was kept])[
  #table(columns: (1.2in, 1fr),
    [Message], [Meaning],
    [#cmd("UFSD097W")], [The abend path is over. The storage is kept and the
      next start frees it. This is the last message of an abend.],
    [#cmd("UFSD097E")], [Supervisor state could not be entered at the end. The
      storage is kept and the active flag stays set. The next start reclaims
      it when the server's address space is gone. If it refuses with
      #cmd("UFSD156W"), run #cmd("UFSDCLNP").],
    [#cmd("UFSD098W")], [Clients were still at work when the stop ran out of
      time. The storage is kept; the next start frees it.],
    [#cmd("UFSD096E")], [The recovery after an abend did not find the control
      block of the server. Nothing was quiesced and the storage is kept. The
      next start frees it.],
    [#cmd("UFSD002E")], [A start was refused because a server is running, or
      because the anchor cannot be checked.],
    [#cmd("UFSD003E")], [A start could not free the storage of an earlier run,
      because it could not enter supervisor state (#cmd("UFSD143E") before it).
      The step ends with return code 8.],
  )
]

The complete list, with the exact text, is in the appendix of the
_UFSD Reference_.

== Changing the Subsystem Name <ug-recover-ssname>

Early versions of UFSD used the subsystem name #cmd("UFSD"). The name was
changed to #cmd("UFS1") in 1.1.0, and it is compiled into every client. A
client built for the old name does not find the new server, and a new server
does not see an old subsystem. On a system that has ever run UFSD 1.0.0, stop
the server with #cmd("P UFSD"), run #cmd("UFSDCLNP") with the old load
modules still in place if an orphan is left, then install the new modules
and every rebuilt client together. Delete a companion procedure
#cmd("UFSD") in #cmd("SYS1.PROCLIB") if there is one, because it hides the
procedure in the other library.
