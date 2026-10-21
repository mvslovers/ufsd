#import "../bookmaster/bookmaster.typ": *

= Removing UFSD <ug-remove>

#idx("removal")
#idx("uninstall")
This chapter takes UFSD off an MVS 3.8j system and frees its FMID, so that
it can be installed again. The main way lets SMP do the work: a dummy
function, #cmd("TUFSDEL"), deletes every release of UFSD, and the load modules
leave both libraries with it. A way that uses UCLIN alone stays as the
fallback (@ug-remove-uclin); the upgrade routes of @ug-install-upgrade use it
too. If you want to move to a newer release, you are in the wrong chapter: a
release deletes its predecessor when it is installed, and the last step below
would scratch the libraries that the new release installs into.

== What UFSD Put on the System <ug-remove-what>

#tab(caption: [What release 1.5.0 installed])[
  #table(columns: (1.6in, 1fr),
    [Item], [Name],
    [FMID], [#cmd("TUFS150")],
    [Load modules], [#cmd("UFSD"), #cmd("UFSDSSIR"), #cmd("UFSDCLNP"), #cmd("UFSFMT")],
    [Target library], [#cmd("UFSD.LINKLIB")],
    [Distribution library], [#cmd("UFSD.AUFSDLOD")],
    [Sample library], [#cmd("UFSD.SAMPLIB")],
  )
]

The data set names carry no version, so there is only one UFSD installation
on a system, whichever release put it there. Releases before 1.3.0 put the
version into the names (#cmd("UFSD.V1R2M2.LINKLIB") and so on). If you find
data sets of this kind, add the qualifier to every name below, and the FMID
is the one of that release.

The staging library #cmd("UFSD.UFSDLOAD") is not on the list, because the
cleanup step of the install job scratched it.

*Earlier releases left their FMIDs behind.* Each upgrade deletes the release
before it and leaves its id in the inventory as a mark, in both zones:

#screen(```
TUFS140   TYPE            = FUNCTION
          DELBY           = TUFS150
```)

A system that went from 1.2.x through 1.3.0 and 1.4.0 to 1.5.0 carries one
such mark for every step. Deleting the id of the current release and of its
predecessor clears only the newest of them. Both ways below therefore name
every id that UFSD has ever used (#cmd("TUFS110"), #cmd("TUFS120"),
#cmd("TUFS130"), #cmd("TUFS140") and #cmd("TUFS150")), and you need not know
which releases the system has seen. The jobs are written for releases up to
1.5.0. A later release adds its own id to every list in this chapter.

== Step 1: Stop the Server <ug-remove-stop>

#screen(```
P UFSD
```)

If common storage is still held afterwards, run #cmd("S UFSDCLNP") before you
go on (@ug-recover-clnp).

== Step 2: Delete the Release with the Dummy Function <ug-remove-dummy>

#idx("TUFSDEL")
#idx("dummy function")
SMP deletes a function when a later function names it in a #cmd("DELETE")
operand, and it removes the load modules of the deleted function from the
libraries on the way. The job below receives, applies and accepts such a
function. #cmd("TUFSDEL") has no elements. It deletes all UFSD ids up to 1.5.0
in one #cmd("DELETE") statement.

*#cmd("TUFSDEL") is not a release FMID.* No release of UFSD ships it, the
job brings it in by itself, and step 3 frees it again in the same flow. Do not
use it for anything else, and do not keep it on the system.

Two things in the job matter:

- The #cmd("APPLY") and the #cmd("ACCEPT") step each carry a
  #cmd("HMASMP.LINKLIB") and a #cmd("HMASMP.AUFSDLOD") statement. SMP deletes
  the modules from the libraries that these DD names point to. Without
  #cmd("HMASMP.AUFSDLOD"), the #cmd("ACCEPT") ends with return code 12 and the
  message #cmd("HMA2833 ** AUFSDLOD DDCARD MISSING"). Without
  #cmd("HMASMP.LINKLIB"), the #cmd("APPLY") still ends with return code 04 and
  prints the same #cmd("HMA2240") lines, but it deletes from the library that
  the procedure names itself (#cmd("SYS1.LINKLIB") on the systems measured),
  and #cmd("UFSD.LINKLIB") keeps its members. The member list of the last step
  is the only proof. The statements come after the #cmd("EXEC") statement and
  before #cmd("SMPCNTL"), as in the install job.
- The later steps run only on #cmd("COND=(4,LT,...)"). The #cmd("RECEIVE") ends
  with return code 04, because a function without elements raises
  #cmd("HMA3971"). With the usual #cmd("COND=(0,NE,...)") the job would stop
  right after the receive.

Submit this job:

#screen(```
//UFSDDEL  JOB (SYS),'UFSD DELETE',
//             CLASS=A,MSGCLASS=H,MSGLEVEL=(1,1),
//             REGION=4096K
//RECV    EXEC SMPREC
//SMPPTFIN DD  *
++FUNCTION(TUFSDEL) .
++VER(Z038) DELETE(TUFS110,TUFS120,TUFS130,TUFS140,TUFS150) .
/*
//SMPCNTL  DD  *
 RESETRC .
 RECEIVE SELECT(TUFSDEL) .
/*
//APPLY   EXEC SMPAPP,COND=(4,LT,RECV.HMASMP)
//HMASMP.LINKLIB  DD  DISP=SHR,DSN=UFSD.LINKLIB
//HMASMP.AUFSDLOD DD  DISP=SHR,DSN=UFSD.AUFSDLOD
//SMPCNTL  DD  *
 APPLY S(TUFSDEL) DIS(WRITE) .
/*
//ACCEPT  EXEC SMPAPP,COND=(4,LT,APPLY.HMASMP)
//HMASMP.LINKLIB  DD  DISP=SHR,DSN=UFSD.LINKLIB
//HMASMP.AUFSDLOD DD  DISP=SHR,DSN=UFSD.AUFSDLOD
//SMPCNTL  DD  *
 ACCEPT S(TUFSDEL) DIS(WRITE) .
/*
//MEMBERS EXEC PGM=IKJEFT01
//SYSTSPRT DD  SYSOUT=*
//SYSTSIN  DD  *
 LISTDS 'UFSD.LINKLIB' MEMBERS
 LISTDS 'UFSD.AUFSDLOD' MEMBERS
/*
//
```)

Read the three SMP steps one by one. The receive ends with return code 04:

#screen(raw(read("../ex/ug-remove/dummy-recv.txt")))

The apply deletes the four load modules from the target library. It also
ends with return code 04, because SMP does not find #cmd("TUFS150") in the
restore data set (#cmd("HMA2461")):

#screen(raw(read("../ex/ug-remove/dummy-apply.txt")))

The accept deletes the four copies from the distribution library and ends
with return code 00:

#screen(raw(read("../ex/ug-remove/dummy-accept.txt")))

*The result is in the last step, not in the return codes.* Both libraries
must list no member. Everything else is the proof that the job ran, not that
the work happened:

#screen(raw(read("../ex/ug-remove/members-empty.txt")))

If a step ends higher than 04, the following #cmd("APPLY") or #cmd("ACCEPT")
step does not run; the last step does, and shows what is still there. Read
the output of the failed step; do not repeat the job before you know what it
did.

*What has been measured.* The job was run on a test system with a fresh
installation of release 1.5.0 as the only SMP product with UFSD on it. A
system with a history of upgrades, a system with more than one product in the
same zones, and a dummy function with a #cmd("++MAC") element instead of none
have not been measured. Whatever the system, the #cmd("LIST") in step 4 is
the proof.

== Step 3: Free the Ids <ug-remove-free>

#idx("UCLIN")
#idx("FMID", "removal")
The dummy function has not made the ids free. It has left #cmd("TUFSDEL")
itself in both zones, with the status #cmd("REC APP ACC"), and a mark
#cmd("DELBY = TUFSDEL") on every id it deleted. This job removes the six
entries from the CDS (what is applied) and the ACDS (what is accepted). It
changes the two SMP data bases and no library:

#screen(```
//UFSDUCL  JOB (SYS),'UFSD UNINSTALL',
//             CLASS=A,MSGCLASS=H,MSGLEVEL=(1,1),
//             REGION=4096K
//UCLCDS  EXEC SMPAPP
//SMPCNTL  DD  *
 UCLIN CDS .
  DEL SYSMOD(TUFSDEL) .
  DEL SYSMOD(TUFS150) .
  DEL SYSMOD(TUFS140) .
  DEL SYSMOD(TUFS130) .
  DEL SYSMOD(TUFS120) .
  DEL SYSMOD(TUFS110) .
 ENDUCL .
/*
//UCLACDS EXEC SMPAPP
//SMPCNTL  DD  *
 UCLIN ACDS .
  DEL SYSMOD(TUFSDEL) .
  DEL SYSMOD(TUFS150) .
  DEL SYSMOD(TUFS140) .
  DEL SYSMOD(TUFS130) .
  DEL SYSMOD(TUFS120) .
  DEL SYSMOD(TUFS110) .
 ENDUCL .
/*
//LIST    EXEC SMPAPP
//SMPCNTL  DD  *
 RESETRC .
 LIST CDS  SYSMOD(TUFSDEL) .
 LIST ACDS SYSMOD(TUFSDEL) .
 LIST CDS  SYSMOD(TUFS150) .
 LIST ACDS SYSMOD(TUFS150) .
 LIST CDS  SYSMOD(TUFS140) .
 LIST ACDS SYSMOD(TUFS140) .
 LIST CDS  SYSMOD(TUFS130) .
 LIST ACDS SYSMOD(TUFS130) .
 LIST CDS  SYSMOD(TUFS120) .
 LIST ACDS SYSMOD(TUFS120) .
 LIST CDS  SYSMOD(TUFS110) .
 LIST ACDS SYSMOD(TUFS110) .
/*
//
```)

Every #cmd("DEL") finds its entry, because the dummy function left a mark for
every id it named, also for those that the system never had. Both #cmd("UCLIN")
steps end with return code 00, and each #cmd("DEL") reports
#cmd("HMA2550 UPDATE COMPLETE"). This is the output of the step
#cmd("UCLCDS"):

#screen(raw(read("../ex/ug-remove/uclcds-free.txt")))

The step #cmd("UCLACDS") gives the same. Any other message is not expected.

== Step 4: Read the LIST <ug-remove-list>

The last step of the job is the result. For every id, in both zones, it must
answer that the entry was not found. These are the first two of the twelve
statements, as the step printed them:

#screen(raw(read("../ex/ug-remove/list-gone.txt")))

and the step must end with the highest return code 04. Return code 04 together
with this answer means that the FMID is free, and #cmd("TUFSDEL") with it.
Both zones count: an id gone from one and still in the other is not free.

*Read the stanzas, not the return code of the step.* A mark that is still
there answers with return code 00, so it does not raise the return code of
the step above the 04 that you expect. The job has twelve #cmd("LIST")
statements, and all twelve must show the id as not found. A stanza that names
#cmd("TYPE = FUNCTION") is an entry that is still there, whether it shows the
status #cmd("REC APP ACC") or only a #cmd("DELBY") of its successor. This is
what the statements #cmd("LIST CDS SYSMOD(TUFS150)") and
#cmd("LIST CDS SYSMOD(TUFSDEL)") print after step 2, before step 3 has run:

#screen(raw(read("../ex/ug-remove/list-present.txt")))

== Step 5: Scratch the Libraries <ug-remove-scratch>

After step 2 the two libraries are empty, but they still exist. The allocation
job of the installation creates both with #cmd("DISP=(NEW,CATLG)"), so it would
fail against either of them. Delete them in a job of your own:

#screen(```
  DELETE UFSD.LINKLIB  NONVSAM SCRATCH PURGE
  DELETE UFSD.AUFSDLOD NONVSAM SCRATCH PURGE
```)

Do this only when you remove UFSD for good. After an upgrade, these are the
data sets that the next release installs into; leave them. #cmd("UFSD.SAMPLIB")
may stay. The install job scratches it itself the next time.

== The Fallback: UCLIN Alone <ug-remove-uclin>

#idx("UCLIN")
Use this way when the dummy function cannot be used, and in the upgrade routes
of @ug-install-upgrade, where the libraries must stay. After step 1, submit
this job instead of steps 2 and 3. It changes the two SMP data bases and no
library:

#screen(```
//UFSDUCL  JOB (SYS),'UFSD UNINSTALL',
//             CLASS=A,MSGCLASS=H,MSGLEVEL=(1,1),
//             REGION=4096K
//UCLCDS  EXEC SMPAPP
//SMPCNTL  DD  *
 UCLIN CDS .
  DEL SYSMOD(TUFS150) MOD(UFSD) .
  DEL SYSMOD(TUFS150) MOD(UFSDSSIR) .
  DEL SYSMOD(TUFS150) MOD(UFSDCLNP) .
  DEL SYSMOD(TUFS150) MOD(UFSFMT) .
  DEL MOD(UFSD) .
  DEL MOD(UFSDSSIR) .
  DEL MOD(UFSDCLNP) .
  DEL MOD(UFSFMT) .
  DEL LMOD(UFSD) .
  DEL LMOD(UFSDSSIR) .
  DEL LMOD(UFSDCLNP) .
  DEL LMOD(UFSFMT) .
  DEL SYSMOD(TUFS150) .
  DEL SYSMOD(TUFS140) .
  DEL SYSMOD(TUFS130) .
  DEL SYSMOD(TUFS120) .
  DEL SYSMOD(TUFS110) .
 ENDUCL .
/*
//UCLACDS EXEC SMPAPP
//SMPCNTL  DD  *
 UCLIN ACDS .
  DEL SYSMOD(TUFS150) MOD(UFSD) .
  DEL SYSMOD(TUFS150) MOD(UFSDSSIR) .
  DEL SYSMOD(TUFS150) MOD(UFSDCLNP) .
  DEL SYSMOD(TUFS150) MOD(UFSFMT) .
  DEL MOD(UFSD) .
  DEL MOD(UFSDSSIR) .
  DEL MOD(UFSDCLNP) .
  DEL MOD(UFSFMT) .
  DEL SYSMOD(TUFS150) .
  DEL SYSMOD(TUFS140) .
  DEL SYSMOD(TUFS130) .
  DEL SYSMOD(TUFS120) .
  DEL SYSMOD(TUFS110) .
 ENDUCL .
/*
//LIST    EXEC SMPAPP
//SMPCNTL  DD  *
 RESETRC .
 LIST CDS  SYSMOD(TUFS150) .
 LIST ACDS SYSMOD(TUFS150) .
 LIST CDS  SYSMOD(TUFS140) .
 LIST ACDS SYSMOD(TUFS140) .
 LIST CDS  SYSMOD(TUFS130) .
 LIST ACDS SYSMOD(TUFS130) .
 LIST CDS  SYSMOD(TUFS120) .
 LIST ACDS SYSMOD(TUFS120) .
 LIST CDS  SYSMOD(TUFS110) .
 LIST ACDS SYSMOD(TUFS110) .
/*
//
```)

Every #cmd("DEL") that finds its entry reports #cmd("HMA2550 UPDATE COMPLETE").
An id that was never in the zone is reported as #cmd("HMA2532") and
#cmd("HMA2560"), and the step ends with return code 08. That is expected for
the ids your system never had; the statements after it are still processed. It
is no sign that a line did what you wanted, and a step that ends with return
code 08 can also hide a statement that failed for another reason. Read every
message, and take the #cmd("LIST") of step 4 as the only proof. This is the
output of the step #cmd("UCLCDS") on a system that had installed 1.5.0 new, so
that #cmd("TUFS130") and the older ids were never in its zone:

#screen(raw(read("../ex/ug-remove/uclcds.txt")))

The step #cmd("UCLACDS") gives the same, without the #cmd("LMOD") statements.
Any other message is not expected.

The two zones have a step each for a reason. After a step has ended with
return code 08, SMP does not execute the next #cmd("UCLIN") of the same
step: it answers #cmd("HMA2293 ** CONTROL STATEMENT IGNORED DUE TO PREVIOUS
ERROR") and does nothing. With both blocks in one step, the ids that the
system never had would leave the ACDS untouched.

Then read the #cmd("LIST") as in step 4, here ten statements without
#cmd("TUFSDEL"), and, if you remove UFSD for good, scratch the libraries as in
step 5. #cmd("UCLIN") changes only the inventory: until you scratch them, the
load modules are still in the target library, and the copies that SMP accepted
are in the distribution library.

== What Is Not Removed <ug-remove-left>

SMP never owned these, so you have to take care of them:

- the procedures and the member of the configuration that you copied into the
  procedure library and the parameter library of the system;
- the entry for #cmd("UFSD.LINKLIB") in the APF list, if you made one;
- *your file systems.* The data sets that #cmd("ROOT") and #cmd("MOUNT") name in
  the member hold your files. None of the steps above touches them.

== Why RESTORE and REJECT Do Not Work <ug-remove-why>

The install job accepts the FMID in the same run in which it applies it. That
closes both of the usual ways back:

#screen(raw(read("../ex/ug-remove/restore-reject.txt")))

#cmd("RESTORE") refuses a SYSMOD that has been accepted
(#cmd("HMA2452"), return code 12). #cmd("REJECT") then fails for a reason that
looks unrelated: #cmd("HMA2462") says that the SYSMOD is not on the
#cmd("SMPPTS") data set, although the job received it from there.

The cause, on the system where this was measured, is the option #cmd("PURGE=YES")
in the system entry of the #cmd("SMPPTS"). With it, SMP deletes a SYSMOD from
the PTS as soon as it has accepted it. The entry shows with #cmd("LIST PTS ."):

#screen(raw(read("../ex/ug-remove/pts-system.txt")))

Two things have not been measured: a system with #cmd("PURGE=NO"), and a
#cmd("REJECT") after the #cmd("ACCEPT") there. No #cmd("UCLIN") statement was
found that changes the system entry, so this book does not change it. It
removes the FMID in ways that do not need the PTS.

The price of the accept is that a later #cmd("RESTORE") of a PTF can return to
the distribution library, and without the accept it would delete the module
instead of reverting it.
