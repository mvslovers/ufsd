#import "../bookmaster/bookmaster.typ": *

= Removing UFSD <ug-remove>

#idx("removal")
#idx("uninstall")
This chapter takes UFSD off an MVS 3.8j system and frees its FMID, so that
it can be installed again. If you want to move to a newer release, you are in
the wrong chapter: a release deletes its predecessor when it is installed
(@ug-install-upgrade), and the last step below would scratch the libraries
that the new release installs into.

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
predecessor clears only the newest of them. The job below therefore names
every id that UFSD has ever used (#cmd("TUFS110"), #cmd("TUFS120"),
#cmd("TUFS130"), #cmd("TUFS140") and #cmd("TUFS150")). An id that was never on
the system is reported as not existing and raises the return code of the step
to 08 (see below). That does no harm, and you need not know which releases the
system has seen.

== Step 1: Stop the Server <ug-remove-stop>

#screen(```
P UFSD
```)

If common storage is still held afterwards, run #cmd("S UFSDCLNP") before you
go on (@ug-recover-clnp).

== Step 2: Remove the FMID from the Inventory <ug-remove-uclin>

#idx("UCLIN")
#idx("FMID", "removal")
Submit this job. It changes the two SMP data bases, the CDS (what is applied)
and the ACDS (what is accepted), and no library:

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
the ids your system never had, and the statements after it are still
processed. This is the output of the step #cmd("UCLCDS") on a system that had
installed 1.5.0 new, so that #cmd("TUFS130") and the older ids were never in
its zone:

#screen(raw(read("../ex/ug-remove/uclcds.txt")))

The step #cmd("UCLACDS") gives the same, without the #cmd("LMOD") statements.
Any other message is not expected.

The two zones have a step each for a reason. After a step has ended with
return code 08, SMP does not execute the next #cmd("UCLIN") of the same
step: it answers #cmd("HMA2293 ** CONTROL STATEMENT IGNORED DUE TO PREVIOUS
ERROR") and does nothing. With both blocks in one step, the ids that the
system never had would leave the ACDS untouched. The #cmd("LIST") step shows
that, as the next section explains.

== Step 3: Read the LIST <ug-remove-list>

The last step of the job is the result. For every id, in both zones, it must
answer that the entry was not found. These are the first two of the ten
statements, as the step printed them:

#screen(raw(read("../ex/ug-remove/list-gone.txt")))

and the step must end with the highest return code 04. Return code 04 together
with this answer means that the FMID is free. Both zones count: an id gone
from one and still in the other is not free.

*Read the stanzas, not the return code of the step.* A mark that is still
there answers with return code 00, so it does not raise the return code of
the step above the 04 that you expect. The job has ten #cmd("LIST")
statements, and all ten must show the id as not found. A stanza that names
#cmd("TYPE = FUNCTION") is an entry that is still there, whether it shows the
status #cmd("REC APP ACC") of the FMID or only a #cmd("DELBY") of its
successor. This is what the two statements #cmd("LIST CDS SYSMOD(TUFS150)") and
#cmd("LIST CDS SYSMOD(TUFS140)") print on a system with 1.5.0 installed:

#screen(raw(read("../ex/ug-remove/list-present.txt")))

== Step 4: Scratch the Libraries <ug-remove-scratch>

#cmd("UCLIN") changes only the inventory. The load modules are still in the
target library, and the copies that SMP accepted are in the distribution
library. The allocation job of the installation creates both with
#cmd("DISP=(NEW,CATLG)"), so it would fail against either of them. Delete
them in a job of your own:

#screen(```
  DELETE UFSD.LINKLIB  NONVSAM SCRATCH PURGE
  DELETE UFSD.AUFSDLOD NONVSAM SCRATCH PURGE
```)

Do this only when you remove UFSD for good. After an upgrade, these are the
data sets that the next release installs into; leave them. #cmd("UFSD.SAMPLIB")
may stay. The install job scratches it itself the next time.

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

#screen(```
HMA2452 ** SYSMOD <fmid> SELECTED FOR RESTORE HAS BEEN ACCEPTED
HMA3703 ** RESTORE PROCESSING TERMINATED BECAUSE FUNCTION SYSMOD
           <fmid> FAILED
HMA2050    RESTORE PROCESSING COMPLETED - HIGHEST RETURN CODE IS 12
```)

#cmd("RESTORE") refuses a SYSMOD that has been accepted. #cmd("REJECT") then
fails for a reason that looks unrelated:

#screen(```
HMA2462 ** SYSMOD <fmid> NOT FOUND ON SMPPTS LIBRARY
HMA2260    REJECT PROCESSING TERMINATED FOR SYSMOD <fmid>
HMA2050    REJECT PROCESSING COMPLETED - HIGHEST RETURN CODE IS 12
```)

The #cmd("ACCEPT") removes the modification control statements from the
#cmd("SMPPTS") data set, and #cmd("REJECT") needs them. #cmd("UCLIN") is not a
workaround: it is the only way. The price of the accept is that a later
#cmd("RESTORE") of a PTF can return to the distribution library, and without
the accept it would delete the module instead of reverting it.
