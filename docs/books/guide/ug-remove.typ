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

#note[If you installed a development build of the main branch, its FMID is
#cmd("TUFS160"), which none of the jobs below names. Go to
@ug-remove-dev first.]

== What UFSD Put on the System <ug-remove-what>

#tab(caption: [What release 1.5.2 installed])[
  #table(columns: (1.6in, 1fr),
    [Item], [Name],
    [FMID], [#cmd("TUFS152")],
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
TUFS151   TYPE            = FUNCTION
          DELBY           = TUFS152
```)

A system that went from 1.2.x through 1.3.0, 1.4.0, 1.5.0 and 1.5.1 to 1.5.2
carries one such mark for every step. Deleting the id of the current release and of its
predecessor clears only the newest of them. Both ways below therefore name
every id that UFSD has ever used (#cmd("TUFS110"), #cmd("TUFS120"),
#cmd("TUFS130"), #cmd("TUFS140"), #cmd("TUFS150"), #cmd("TUFS151") and #cmd("TUFS152")), and you
need not know which releases the system has seen. The jobs are written for
releases up to 1.5.2. A later release adds its own id to every list in this chapter.

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
function. #cmd("TUFSDEL") has no elements. It deletes all UFSD ids up to 1.5.2
in one #cmd("DELETE") statement.

*#cmd("TUFSDEL") is not a release FMID.* No release of UFSD ships it, the
job brings it in by itself, and step 3 frees it again in the same flow. Do not
use it for anything else, and do not keep it on the system.

Three things in the job matter:

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
- The #cmd("++VER") statement is on two lines, and it has to stay so. SMP reads
  columns 1 to 72 of a card and nothing after them. With all seven ids on one
  line, the last id crosses column 72, and the #cmd("RECEIVE") ends with return
  code 08 and the messages #cmd("HMA2032 ** SYNTAX ERROR IN CONTROL STATEMENT AT
  COL 68") and #cmd("HMA3962 ** SYSMOD TUFSDEL HAS NO ++VER MODIFICATION CONTROL
  STATEMENT"). The #cmd("APPLY") step is then flushed, and the #cmd("ACCEPT")
  step runs all the same and ends with return code 12 (#cmd("HMA4012 ** SYSMOD
  TUFSDEL SELECTED FOR ACCEPT NOT FOUND ON SMPPTS LIBRARY")). Nothing is
  removed. A statement with six ids, as in the earlier releases of this
  chapter, fitted on the line.

Submit this job:

#screen(```
//UFSDDEL  JOB (SYS),'UFSD DELETE',
//             CLASS=A,MSGCLASS=H,MSGLEVEL=(1,1),
//             REGION=4096K
//RECV    EXEC SMPREC
//SMPPTFIN DD  *
++FUNCTION(TUFSDEL) .
++VER(Z038) DELETE(TUFS110,TUFS120,TUFS130,TUFS140,TUFS150,
   TUFS151,TUFS152) .
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
ends with return code 04, because SMP does not find #cmd("TUFS152") in the
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
installation of release 1.5.2 as the only SMP product with UFSD on it, and on
one that went from a fresh 1.5.1 to 1.5.2 by the upgrade of the installation
guide. A system with more than one product in the same zones, and
a dummy function with a #cmd("++MAC") element instead of none have not been
measured. Whatever the system, the #cmd("LIST") in step 4 is
the proof.

== Step 3: Free the Ids <ug-remove-free>

#idx("UCLIN")
#idx("FMID", "removal")
The dummy function has not made the ids free. It has left #cmd("TUFSDEL")
itself in both zones, with the status #cmd("REC APP ACC"), and a mark
#cmd("DELBY = TUFSDEL") on every id it deleted. This job removes the eight
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
  DEL SYSMOD(TUFS152) .
  DEL SYSMOD(TUFS151) .
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
  DEL SYSMOD(TUFS152) .
  DEL SYSMOD(TUFS151) .
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
 LIST CDS  SYSMOD(TUFS152) .
 LIST ACDS SYSMOD(TUFS152) .
 LIST CDS  SYSMOD(TUFS151) .
 LIST ACDS SYSMOD(TUFS151) .
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
answer that the entry was not found. These are the first two of the sixteen
statements, as the step printed them:

#screen(raw(read("../ex/ug-remove/list-gone.txt")))

and the step must end with the highest return code 04. Return code 04 together
with this answer means that the FMID is free, and #cmd("TUFSDEL") with it.
Both zones count: an id gone from one and still in the other is not free.

*Read the stanzas, not the return code of the step.* A mark that is still
there answers with return code 00, so it does not raise the return code of
the step above the 04 that you expect. The job has sixteen #cmd("LIST")
statements, and all sixteen must show the id as not found. A stanza that names
#cmd("TYPE = FUNCTION") is an entry that is still there, whether it shows the
status #cmd("REC APP ACC") or only a #cmd("DELBY") of its successor. This is
what the statements #cmd("LIST CDS SYSMOD(TUFS152)") and
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
  DEL SYSMOD(TUFS152) MOD(UFSD) .
  DEL SYSMOD(TUFS152) MOD(UFSDSSIR) .
  DEL SYSMOD(TUFS152) MOD(UFSDCLNP) .
  DEL SYSMOD(TUFS152) MOD(UFSFMT) .
  DEL MOD(UFSD) .
  DEL MOD(UFSDSSIR) .
  DEL MOD(UFSDCLNP) .
  DEL MOD(UFSFMT) .
  DEL LMOD(UFSD) .
  DEL LMOD(UFSDSSIR) .
  DEL LMOD(UFSDCLNP) .
  DEL LMOD(UFSFMT) .
  DEL SYSMOD(TUFS152) .
  DEL SYSMOD(TUFS151) .
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
  DEL SYSMOD(TUFS152) MOD(UFSD) .
  DEL SYSMOD(TUFS152) MOD(UFSDSSIR) .
  DEL SYSMOD(TUFS152) MOD(UFSDCLNP) .
  DEL SYSMOD(TUFS152) MOD(UFSFMT) .
  DEL MOD(UFSD) .
  DEL MOD(UFSDSSIR) .
  DEL MOD(UFSDCLNP) .
  DEL MOD(UFSFMT) .
  DEL SYSMOD(TUFS152) .
  DEL SYSMOD(TUFS151) .
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
 LIST CDS  SYSMOD(TUFS152) .
 LIST ACDS SYSMOD(TUFS152) .
 LIST CDS  SYSMOD(TUFS151) .
 LIST ACDS SYSMOD(TUFS151) .
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
output of the step #cmd("UCLCDS") on a system that had installed 1.5.2 new, so
that #cmd("TUFS150") and the older ids were never in its zone:

#screen(raw(read("../ex/ug-remove/uclcds.txt")))

The step #cmd("UCLACDS") gives the same, without the #cmd("LMOD") statements.
Any other message is not expected.

The two zones have a step each for a reason. After a step has ended with
return code 08, SMP does not execute the next #cmd("UCLIN") of the same
step: it answers #cmd("HMA2293 ** CONTROL STATEMENT IGNORED DUE TO PREVIOUS
ERROR") and does nothing. With both blocks in one step, the ids that the
system never had would leave the ACDS untouched.

Then read the #cmd("LIST") as in step 4, here fourteen statements without
#cmd("TUFSDEL"), and, if you remove UFSD for good, scratch the libraries as in
step 5. #cmd("UCLIN") changes only the inventory: until you scratch them, the
load modules are still in the target library, and the copies that SMP accepted
are in the distribution library.

== You Installed a Development Build with a Later FMID <ug-remove-dev>

#idx("development build")
#idx("TUFS160")
A development build of the main branch is packaged under the FMID of the
release that the branch is heading for. A build of 1.6.0-dev carries
#cmd("TUFS160"), although 1.6.0 is not released. If you installed such a package
by SMP, the system holds an id that no release of this book knows. Neither the
dummy function of step 2 nor the lists of step 3 name it, so it stays in both
zones, and it still owns the load modules. Install a release only after you have
cleaned it up as described here.

A package that was built with a throwaway id of its own is the same case with
another name. Wherever this section says #cmd("TUFS160"), read your id, and name
it in the same places. A current #cmd("mbt") refuses to package a pre-release
without such an id, so a package of the first kind comes from an older
#cmd("mbt").

Ask SMP which id owns the load module. This job lists the module in both zones:

#screen(```
//UFSDLST  JOB (SYS),'UFSD LIST',
//             CLASS=A,MSGCLASS=H,MSGLEVEL=(1,1),
//             REGION=4096K
//LIST    EXEC SMPAPP
//SMPCNTL  DD  *
 LIST CDS  MOD(UFSD) .
 LIST ACDS MOD(UFSD) .
/*
//
```)

Read the #cmd("FMID") line of the stanza (the stanzas are shown in step 6 below).
The releases of UFSD have used these ids, and no others: #cmd("TUFS110"),
#cmd("TUFS120"), #cmd("TUFS130"), #cmd("TUFS140"), #cmd("TUFS150"),
#cmd("TUFS151") and #cmd("TUFS152"). If the #cmd("FMID") is one of them, the
system belongs to the main way and this section is not for you. If it is any
other id, such as #cmd("TUFS160"), you are in the right section.

The steps are those of the main way, with two differences. #cmd("TUFS160") is
named in every list, and the libraries stay.

*Step 1.* Stop the server as in @ug-remove-stop.

*Step 2.* Submit the job of @ug-remove-dummy with one change: #cmd("TUFS160") is
added to the #cmd("DELETE") operand, as the last id on the second line of the
#cmd("++VER") card. Everything else stays as it is there. Keep the card on two
lines. With the eight ids on one line it is 85 characters long, and SMP reads
columns 1 to 72 and nothing after them. The second line, with #cmd("TUFS160") on
it, is 29 characters long. The receive ends with return code 04 and echoes the
card:

#screen(raw(read("../ex/ug-remove/dev-recv.txt")))

The apply deletes the four load modules and ends with return code 04. The
#cmd("HMA2461") line names #cmd("TUFS160"), the id that the system had applied,
and not #cmd("TUFS152") as in step 2:

#screen(raw(read("../ex/ug-remove/dev-apply.txt")))

The accept prints the four #cmd("HMA2240") lines for #cmd("AUFSDLOD") as in
step 2 and ends with return code 00. Both member lists must be empty, as in
step 2. If the receive ended with 08, the job removed nothing: do not run
step 3 then, because UCLIN changes only the inventory and leaves the load
modules where they are.

*Step 3.* Submit the job of @ug-remove-free with #cmd("TUFS160") added to the
#cmd("DEL") statements of both UCLIN blocks and to the #cmd("LIST") statements.
In the job that was run, #cmd("TUFS160") is the second #cmd("DEL") statement of
each block, right after #cmd("TUFSDEL") and ahead of #cmd("TUFS152"). In the
#cmd("LIST") step it follows #cmd("TUFSDEL") in the same way (#cmd("LIST CDS")
for #cmd("TUFSDEL") and #cmd("TUFS160"), then #cmd("LIST ACDS") for both), and
the eight pairs of the main way come after. Each zone now takes nine #cmd("DEL")
statements, #cmd("TUFSDEL") and the eight ids, and the #cmd("LIST") step has
eighteen statements. Both UCLIN steps end with return code 00, and all
nine #cmd("DEL") statements of each report #cmd("HMA2550"). The second zone
needs no statement beyond these. The step #cmd("UCLCDS") begins like this:

#screen(raw(read("../ex/ug-remove/dev-uclcds.txt")))

Step #cmd("UCLACDS") gives the same. Read the #cmd("LIST") as in
@ug-remove-list: all eighteen statements must show their id as not found, and
the step ends with return code 04. A stanza with #cmd("TYPE = FUNCTION") for
#cmd("TUFS160") is an entry that is still there, in whichever zone it shows.

*Step 4.* Do not scratch the libraries. Leave out @ug-remove-scratch. The
release installs into #cmd("UFSD.LINKLIB") and #cmd("UFSD.AUFSDLOD"), which are
empty now.

*Step 5.* Install the release as in @ug-install-job, with the install job of
the release package. *Do not run the allocation job.* It allocates both
libraries #cmd("NEW"), and they exist. That the job fails on them has not been
measured: the test runs skipped it. The receive, the apply
check, the apply and the accept end with return code 00. The apply copies each
of the four modules into #cmd("UFSD.LINKLIB"), and the accept into
#cmd("UFSD.AUFSDLOD"). There is no #cmd("HMA2461") line. These are the lines of
the apply:

#screen(raw(read("../ex/ug-remove/dev-install-apply.txt")))

The accept prints the same four lines with #cmd("LIBRARY=AUFSDLOD").

*Step 6.* List the members of both libraries: #cmd("UFSD"), #cmd("UFSDCLNP"),
#cmd("UFSDSSIR") and #cmd("UFSFMT") must be in both. The two #cmd("LIST")
statements must show the release, in both zones, as the owner of the module,
and not #cmd("TUFS160"):

#screen(raw(read("../ex/ug-remove/dev-owner.txt")))

#cmd("TUFS160") is not found in either zone, and neither are #cmd("TUFS150"),
#cmd("TUFS140") and #cmd("TUFSDEL"). #cmd("TUFS151") is there with
#cmd("DELBY = TUFS152"): that is the mark which the release leaves for its
predecessor, as described in @ug-remove-what, and not a rest of the development
build. Start UFSD and read the banner; it must name the release, and must not
end in #cmd("-DEV"):

#screen(raw(read("../ex/ug-remove/dev-banner.txt")))

A banner with #cmd("-DEV") after a clean install means that the server was
loaded from another library. A development build is deployed to
#cmd("UFSD.DEV.LINKLIB"), and the #cmd("STEPLIB") of the procedure names that
library ahead of #cmd("UFSD.LINKLIB"). The SMP steps above act on
#cmd("UFSD.LINKLIB") alone, so they leave a copy in #cmd("UFSD.DEV.LINKLIB")
where it is. Delete the library, or take it out of the #cmd("STEPLIB"), and
start the server again. This has not been measured for UFSD; it was measured on
another product built the same way.

*What happens without this section.* The 1.5.2 install job ends with return code
00 in all its steps, and prints no warning. In the apply check, the apply and
the accept, each of the four modules is #cmd("NOT SEL"): #cmd("TUFS160") still
owns it, and #cmd("TUFS152") is installed but owns nothing. Both libraries are
unchanged, the #cmd("LIST") statements still show #cmd("TUFS160") as
#cmd("FMID") and #cmd("RMID"), and the server starts as the development build:

#screen(raw(read("../ex/ug-remove/dev-banner-control.txt")))

The return codes do not tell you this. The banner and the #cmd("FMID") do.

*What has been measured.* The section was followed on two test systems, an
MVS/CE and a TK5, each with a development build of release 1.6.0 installed under
#cmd("TUFS160") by SMP, and the release 1.5.2 (the released one, not a
candidate) installed afterwards as above. The control run, without the section,
was made on both as well. The two systems did not start from the same state:

- On the MVS/CE system the apply of the development build ended without
  #cmd("HMA2461"). Nothing was recorded about an earlier UFSD there.
- On the TK5 system the development build was installed over release 1.4.0. Its
  apply ended with return code 04 and #cmd("HMA2461 SYSMOD TUFS140 NOT FOUND ON
  SMPSCDS LIBRARY"), so #cmd("TUFS140") was in the history there and went with
  the lists.

Both runs gave the results shown above.

*What has not been measured.* A system that really went through the earlier
releases, a second development build installed over the first (#cmd("TUFS160"),
then a later id), a throwaway id of your own instead of #cmd("TUFS160"), a
development build installed over a system that already holds release 1.5.2, the
removal without a release afterwards (see below), and a copy in
#cmd("UFSD.DEV.LINKLIB").

*If you want to remove UFSD and not install a release.* Steps 1 to 3 above clear
the development build from both zones and both libraries. UFSD is then gone from
SMP, apart from the two empty libraries, which @ug-remove-scratch scratches.
That combination, steps 1 to 3 of this section followed by the scratch step of
the main way, has not been run. Check it as you check the main way: the
libraries are gone, and #cmd("LIST CDS MOD(UFSD)") and #cmd("LIST ACDS
MOD(UFSD)") find nothing.

== What Is Not Removed <ug-remove-left>

SMP never owned these, so you have to take care of them:

- the procedures and the member of the configuration that you copied into the
  procedure library and the parameter library of the system;
- the entry for #cmd("UFSD.LINKLIB") in the APF list, if you made one;
- #cmd("UFSD.DEV.LINKLIB"), if you deployed a development build there. SMP never
  wrote it, and neither the steps above nor the install of a release clear it;
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
