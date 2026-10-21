#import "../bookmaster/bookmaster.typ": *

= Installing UFSD <ug-install>

#idx("installation")
This chapter installs UFSD 1.5.0 on an MVS 3.8j system (TK4-, TK5, MVS/CE or
a Hercules build of your own) from the release package. The installation is
done by SMP Release 4, the SMP that comes with MVS 3.8j, not SMP/E. SMP keeps
a record of what was installed, and so there is a defined way to remove it
again (@ug-remove).

Report a problem as an issue at https://github.com/mvslovers/ufsd/issues.
Add the two build stamps that UFSD writes at the start (#cmd("UFSD000I") and
#cmd("UFSD005I"), @ug-install-start), the console messages and, for an
installation problem, the job output. Questions go to the Discord server of
the project.

The names of the data sets carry no version. #cmd("UFSD.LINKLIB") is the
same name in every release. The #cmd("STEPLIB") of the procedure, client JCL
that names the library and an APF entry are written once. The price is that
two releases cannot be installed side by side: an upgrade replaces what is
there. There is no way back to the previous release. The install job accepts
the new level, and SMP refuses a #cmd("RESTORE") of an accepted function
(@ug-remove-why). To take UFSD out of the system, follow @ug-remove.

== What Is in the Package <ug-install-files>

#idx("package", "contents")
The package #cmd("ufsd-1.5.0-dist.zip") (or #cmd("ufsd-1.5.0-dist.tar.gz")) has
five files. In their names, 1.5.0 is the release.

#tab(caption: [The files of the installation package])[
  #table(columns: (2.7in, 1fr),
    [File], [What it is],
    [#cmd("README.md")], [The installation guide as a text file.],
    [#cmd("ufsd-1.5.0-load.xmit")], [The four load modules, in a form that
      TSO RECEIVE reads.],
    [#cmd("ufsd-1.5.0-samplib.xmit")], [The sample library: two
      procedures, a configuration pattern and a format job.],
    [#cmd("ufsd-1.5.0-alloc.jcl")], [Allocates the data sets SMP installs
      into. Run once.],
    [#cmd("ufsd-1.5.0-inst.jcl")], [Receives both XMIT files and installs
      them. Can be repeated.],
  )
]

The load modules are #cmd("UFSD"), #cmd("UFSDSSIR"), #cmd("UFSDCLNP") and
#cmd("UFSFMT"). When the installation is done, they are here:

#deflist(width: 1.6in,
  [#cmd("UFSD.LINKLIB")], [The load modules. The #cmd("STEPLIB") points here.],
  [#cmd("UFSD.SAMPLIB")], [The patterns that you copy in @ug-install-proc.],
  [#cmd("UFSD.AUFSDLOD")], [The distribution library of SMP. A later
    PTF can be restored to the level in it. It is not a way back to the
    previous release (@ug-remove-why).],
)

The fourth file of the release, #cmd("ufsd-1.5.0-lib.tar.gz"), is not part of
the package and is not needed to install UFSD. It holds #cmd("libufs") and
its headers for programs that want to use UFSD.

== What You Need <ug-install-prereq>

#idx("prerequisites")
- MVS 3.8j running on Hercules, with SMP 4 in working order: the
  procedures #cmd("SMPREC") and #cmd("SMPAPP") and the #cmd("SYS1.SMP*") data
  sets. They are there on TK4-, TK5 and MVS/CE as delivered.
- A user ID that may submit jobs, change a procedure library of the started
  tasks and change a PARMLIB.
- A way to send a file to the host in binary (@ug-install-upload).
- About 15 cylinders of disk space for the data sets of the product.

=== Authority <ug-install-auth>

#idx("authorization")
#idx("APF")
UFSD enters supervisor state and works in key-0 common storage, so it must
run authorised. There are two ways to get that.

*Through RAKF.* UFSD asks for authority itself at the start. The request is
the SVC 244, which RAKF supplies. TK4- and TK5 have RAKF, so UFSD needs no
other setup there. The start shows #cmd("UFSD007I AUTHORIZED BY SVC (MODULE KEY 8)").

*Through the APF list.* Add #cmd("UFSD.LINKLIB") to
#cmd("SYS1.PARMLIB(IEAAPF00)"). MVS 3.8j reads the APF list at the IPL only,
so the entry costs one IPL. The name of the library does not change in
later releases, so it costs only that one. The start shows
#cmd("UFSD007I AUTHORIZED BY LIBRARY (MODULE KEY 0)").

If neither way is open, the start ends with this message, even after a clean
installation:

#screen(```
UFSD091E APF SETUP FAILED RC=n (STEPLIB NOT APF AUTHORIZED?)
```)

Settle the question before you start UFSD for the first time.

#note[The release 1.5.0 was tested on the RAKF route. A start from an
APF-authorised library was not run for this release, and neither was the
change of the APF list on MVS/CE.]

== Sending the Two XMIT Files <ug-install-upload>

#idx("upload")
Both XMIT files are EBCDIC NETDATA streams. Send them in binary: no
translation between ASCII and EBCDIC, no conversion of line ends. They go
into sequential data sets with #cmd("RECFM=FB"), #cmd("LRECL=80") and
#cmd("BLKSIZE=3120").

You choose the names of the data sets. The install job names them in its
own #cmd("RECEIVE") commands (@ug-install-job). This book uses
#cmd("IBMUSER.UFSD.LOAD.XMIT") and #cmd("IBMUSER.UFSD.SAMP.XMIT").

Some ways to send need a data set that already exists. Where this book says
_pre-allocate_, allocate both as #cmd("DSORG=PS"), #cmd("RECFM=FB"),
#cmd("LRECL=80") and #cmd("BLKSIZE=3120"), with a primary of about 50
tracks and a secondary of 20 (TSO or ISPF 3.2). Use one of these ways:

#deflist(width: 1.6in,
  [FTPD (mvslovers/ftpd), port 2121, no pre-allocation],
  [Log in, switch to binary and put each file. FTPD creates the data set.
   A #cmd("quote site") is accepted, but its parameters are ignored.],
  [mvsMF with zowe, pre-allocate],
  [#cmd("zowe zos-files upload file-to-data-set") with the option
   #cmd("--binary"), once for each file.],
  [Another FTP server, pre-allocate],
  [Switch to binary and put each file into the existing data set. The
   #cmd("SITE") keywords for data set attributes differ between the TCP/IP
   stacks of MVS 3.8j and are not the z/OS ones, so pre-allocating is the safe
   way.],
  [#cmd("IND$FILE") of a 3270 emulator, no pre-allocation],
  [Use the file transfer of the emulator in binary mode, with the attributes
   above. The syntax of the options depends on the emulator.],
)

An example with the FTPD of the project:

#screen(```
ftp -P 2121 your-mvs-host
> binary
> put ufsd-1.5.0-load.xmit    'IBMUSER.UFSD.LOAD.XMIT'
> put ufsd-1.5.0-samplib.xmit 'IBMUSER.UFSD.SAMP.XMIT'
> quit
```)

If a file arrives in text mode, the install job fails later with an
S106 abend (@ug-install-trouble).

== Allocating the Product Data Sets <ug-install-alloc>

#idx("alloc.jcl")
Submit #cmd("ufsd-1.5.0-alloc.jcl") as it is. The only things worth a change
are the #cmd("UNIT=SYSDA") and the space on each DD statement.

The job creates #cmd("UFSD.LINKLIB") and #cmd("UFSD.AUFSDLOD") and nothing
else. The libraries that the next step receives into are not allocated
here, on purpose: TSO RECEIVE creates its own target and refuses to merge
into an existing data set. The expected result is #cmd("COND CODE 0000").

#note[Run this job once for the life of the system, not once for each
release. The names carry no version, so a later upgrade finds both data sets
and skips this step. A second run fails on #cmd("DISP=(NEW,CATLG)") and
changes nothing. The job has no DELETE step on purpose: #cmd("UFSD.AUFSDLOD")
holds the accepted copy of every module, and a re-run that scratched it
would leave the SMP records describing an installation that is no longer
there. To start over, remove the SYSMOD first (@ug-remove).]

== Running the Install Job <ug-install-job>

#idx("inst.jcl")
Open #cmd("ufsd-1.5.0-inst.jcl") and replace the two placeholder data set
names with the names you sent in @ug-install-upload:

#screen(```
  RECEIVE INDSN('CHANGE.ME.UFSDLOAD') -      <- IBMUSER.UFSD.LOAD.XMIT
  RECEIVE INDSN('CHANGE.ME.SAMPLIB') -       <- IBMUSER.UFSD.SAMP.XMIT
```)

On TK5 these are the only lines to change. On MVS/CE, TSO RECEIVE also needs
a volume to allocate on. Add a #cmd("VOLUME()") operand to both
#cmd("RECEIVE") commands (the steps #cmd("RECV1") and #cmd("RECV2")),
#cmd("PUB000") on a stock MVS/CE or another volume of yours:

#screen(```
  RECEIVE INDSN('IBMUSER.UFSD.LOAD.XMIT') -
    DATASET('UFSD.UFSDLOAD') -
    VOLUME('PUB000')
```)

Submit the job. It has eight steps, and each runs only if the one before it
ended well, so the job stops at the first failure.

#tab(caption: [The steps of the install job])[
  #table(columns: (1.1in, 1fr),
    [Step], [What it does],
    [#cmd("DELOLD")], [Scratches the targets of the RECEIVE, so that the job
      can be run again.],
    [#cmd("RECV1")], [Receives the load XMIT into #cmd("UFSD.UFSDLOAD"), a
      staging library.],
    [#cmd("RECV2")], [Receives the samplib XMIT into #cmd("UFSD.SAMPLIB").],
    [#cmd("RECV")], [Receives the SYSMOD into the SMP inventory. The SYSMOD
      is inline in the job; there is no third file.],
    [#cmd("APPLYCHK")], [A dry run. The #cmd("APPLY") runs only if this one
      ends with RC 0.],
    [#cmd("APPLY")], [Copies the load modules into #cmd("UFSD.LINKLIB").],
    [#cmd("ACCEPT")], [Makes this level the base that the
      #cmd("RESTORE") of a later PTF returns to. It also closes
      #cmd("RESTORE") and #cmd("REJECT") for this release itself
      (@ug-remove-why).],
    [#cmd("CLEANUP")], [Scratches the staging library, which is spent now.],
  )
]

=== What a Good Run Looks Like <ug-install-good>

On a new installation, every step ends with #cmd("COND CODE 0000"). In the SMP
output of the #cmd("APPLY") step, #cmd("HMA2380") appears once for each module
and the step ends with return code 0:

#screen(raw(read("../ex/ug-install/apply-new.txt")))

The step then prints more reports; the screen shows the first. The status
#cmd("TUFS140 DELETED") also appears on a new installation, because the SYSMOD
names TUFS140 in its #cmd("DELETE") operand. The
#cmd("ACCEPT") step has the same messages for the library #cmd("AUFSDLOD").

In an upgrade, the element reports that follow can show #cmd("NOT SEL") next
to #cmd("TUFS140 DELETED"). That is normal. What shows that the upgrade
happened is the pair #cmd("HMA2240") and #cmd("HMA2380") for each module (see
@ug-install-from14). A #cmd("NOT SEL") in place of #cmd("HMA2380") is the
failure described in @ug-install-verify.

SMP copies the modules and does not link them again, so the link attributes
(among them AC(1)) are exactly those of the build.

=== List the Members <ug-install-verify>

#idx("verification")
The return codes show that the job ran. They do not show that the work
happened: SMP names the library by its ddname, and a ddname says nothing
about the data set behind it. An installation that owns none of its
elements ends with RC 00 in every step and an empty library. So list the
members, and take that as the result. ISPF 3.4 does it, or submit this job
with the volume serial of the library in place of #var("volser"):

#screen(```
//UFSDLIST JOB (SYS),'UFSD MEMBERS',
//             CLASS=A,MSGCLASS=H,MSGLEVEL=(1,1)
//LIST    EXEC PGM=IEHLIST
//SYSPRINT DD  SYSOUT=*
//DD1      DD  DISP=SHR,DSN=UFSD.LINKLIB
//SYSIN    DD  *
 LISTPDS DSNAME=UFSD.LINKLIB,VOL=SYSDA=volser
/*
//
```)

The volume serial is in the message #cmd("IEF285I ... VOL SER NOS=") of the
allocation job, or in ISPF 3.4. The job lists the directory:

#screen(raw(read("../ex/ug-install/members.txt")))

The members #cmd("UFSD"), #cmd("UFSDSSIR"), #cmd("UFSDCLNP") and
#cmd("UFSFMT") must all be there. The first number behind each name is the
place of the member on the volume and differs from library to library.

If the list is empty although the job said success, the #cmd("APPLY") printed
#cmd("NOT SEL") instead of #cmd("HMA2380"). Another SYSMOD owns the module
names in the SMP inventory. Each release deletes only the release just
before it, so the owner goes away with the installation only if you come from
that release. The usual cause is a skipped release (@ug-install-upgrade).
The command #cmd("LIST CDS MOD(UFSD) .") names the owner. The UCLIN job of the
fallback in the removal chapter (@ug-remove-uclin) removes it.

== Procedures and Configuration <ug-install-proc>

#idx("procedure")
#idx("PARMLIB")
SMP does not touch your procedure library or your PARMLIB. The product owns
the patterns, and your system owns the copies. If SMP owned the running
procedure, every change you made to it would be replaced by the next update.
So this step is yours, and it is the one place where you must read what you
copy.

Copy these members from #cmd("UFSD.SAMPLIB"):

#tab(caption: [The members of the sample library])[
  #table(columns: (1.2in, 1.9in, 1fr),
    [Member], [Copy it to], [Change],
    [#cmd("UFSD")], [a procedure library in the started-task concatenation],
      [Usually nothing: #cmd("STEPLIB") names #cmd("UFSD.LINKLIB").],
    [#cmd("UFSDCLNP")], [the same procedure library], [Likewise.],
    [#cmd("UFSDPRM0")], [a PARMLIB], [Yes, see below.],
    [#cmd("UFSFMT")], [wherever you keep JCL; it is a job, not a procedure],
      [The data set name and the size (@ug-disks).],
  )
]

A procedure library of the started tasks is, for example,
#cmd("SYS2.PROCLIB"). Look at the master JCL of your system if you are not
sure which libraries the started tasks search.

=== Which PARMLIB <ug-install-parmlib>

The procedure #cmd("UFSD") has two symbolic parameters. #cmd("D") names the
PARMLIB and defaults to #cmd("SYS2.PARMLIB"). #cmd("M") names the member and
defaults to #cmd("UFSDPRM0"). A second configuration can live next to the
first as another member, chosen with #cmd("M=").

On TK5 the data set #cmd("SYS2.PARMLIB") does not exist. Put the member in
#cmd("SYS1.PARMLIB") and either change the #cmd("D=") in your copy of the
procedure or override it when you start:

#screen(```
/S UFSD,D='SYS1.PARMLIB'
```)

On MVS/CE the default is expected to be right, because #cmd("SYS2.PARMLIB")
exists there. That was not checked on a running MVS/CE for this release.

=== Edit UFSDPRM0 <ug-install-prm>

The member that comes with UFSD is a pattern, not a working configuration.
It names data sets of the development system. Every data set named in a
#cmd("ROOT") or #cmd("MOUNT") statement must exist and be formatted before
UFSD starts. UFSD does not allocate or format one. The pattern reads as
follows. This copy is abridged: the member in #cmd("UFSD.SAMPLIB") has two
more #cmd("MOUNT") statements for the home directories #cmd("MVSCE01") and
#cmd("MVSCE02"), a commented-out #cmd("MOUNT") for #cmd("UFSD.ARCHIVE"), and
comments.

#screen(```
ROOT     DSN(UFSD.ROOT)

MOUNT    DSN(HTTPD.WEBROOT.UFS)  PATH(/www)          MODE(RO)
MOUNT    DSN(IBMUSER.UFSHOME)    PATH(/u/ibmuser)    MODE(RW) OWNER(IBMUSER)
MOUNT    DSN(UFSD.SCRATCH)       PATH(/tmp)          MODE(RW)
```)

Remove every #cmd("MOUNT") statement whose data set you will not create,
or turn it into a comment with a #cmd("#") in the first non-blank column.
The statements are described in @ug-config.

== Creating the Disks <ug-install-disks>

Each mounted file system is one data set that holds a formatted image. You
need at least the data set of the root before the first start. @ug-disks
describes how to size, allocate and format them with the job
#cmd("UFSD.SAMPLIB(UFSFMT)").

== Starting UFSD and Checking the Start <ug-install-start>

#idx("start")
Start UFSD from the console:

#screen(```
/S UFSD                      default member (UFSDPRM0)
/S UFSD,M=UFSDPRM1           another member
/S UFSD,D='SYS1.PARMLIB'     another PARMLIB, needed on TK5
```)

A start that went well ends with #cmd("UFSD001I ... READY"). The set of
lines depends on your configuration. This is the console of a system with
three file systems:

#screen(raw(read("../ex/ug-ops/start.txt")))

Two lines identify the build. #cmd("UFSD000I") gives the version and the
commit the module was built from. #cmd("UFSD005I") gives the version of
LIBC/370 (libc370) it was linked with. Quote both in a bug report. A build
made from a changed working tree marks its hash with #cmd("-DIRTY") and
adds #cmd("UFSD006W"); a released build never does.

The root is shown as #cmd("(ROOT)") and not as #cmd("RW") or #cmd("RO"). It is
mounted for writing only as long as UFSD needs to create the mount-point
directories, and then it is switched to read only, before the first client
can reach it. #cmd("UFSD040I") is only a count. For the state of each file
system use #cmd("/F UFSD,MOUNT LIST") (@ug-ops-mount).

The complete order of the messages of a start is in @ug-ops-start.

== Upgrading from an Earlier Release <ug-install-upgrade>

#idx("upgrade")
Ask SMP first which FMID owns the modules, because each release deletes only
the one just before it. Run this under the SMP procedure:

#screen(```
//LIST    EXEC SMPAPP
//SMPCNTL  DD  *
 LIST CDS  MOD(UFSD) .
 LIST ACDS MOD(UFSD) .
/*
```)

#tab(caption: [What the owner of MOD(UFSD) tells you])[
  #table(columns: (1in, 1fr),
    [Owner], [What to do],
    [#cmd("TUFS140")], [An upgrade from 1.4.0. Stop UFSD, run the install
      job, start UFSD.],
    [#cmd("TUFS130")], [From 1.3.0. This release does not delete 1.3.0, so
      read the part for 1.3.0 below.],
    [#cmd("TUFS120")], [From 1.2.x. Read the part for 1.2.x below.],
    [not found], [A new installation, or a system that never had UFSD under
      SMP. For the second case read the part for the system without SMP
      below. Then run the allocation job and the install job.],
  )
]

Every release has an FMID of its own and deletes only that of its
predecessor. #cmd("TUFS140") is 1.4.0, and #cmd("TUFS150"), the FMID of this
release, deletes #cmd("TUFS140"). An older owner of #cmd("MOD(UFSD)") stays,
and then SMP installs nothing.

=== From 1.4.0 <ug-install-from14>

An upgrade from the release just before is the steps in @ug-install-upload
and @ug-install-job and nothing else. The names of the data sets do not
change, and the SYSMOD removes its predecessor itself.

- *Skip the allocation job.* #cmd("UFSD.LINKLIB") and #cmd("UFSD.AUFSDLOD")
  exist already. The job would fail on #cmd("DISP=(NEW,CATLG)"), harmlessly,
  and has nothing to do.
- *There is no removal step.* The ways of @ug-remove take UFSD away. They do
  not replace it, and in an upgrade the dummy function would empty and the
  scratch step would delete the data sets that you install into.
- *Stop the server first:* #cmd("/P UFSD") before the install job and
  #cmd("/S UFSD") after it. A running task keeps its own copy of the load
  module in storage and goes on using it, so an upgrade without this stop
  looks as if it had done nothing.
- *Expect RC 04 from the APPLY and let it stand.* A deleted FMID has no
  backup entry in #cmd("SYS1.SMPSCDS") and never will. The step reports
  success, and then a 4:

#screen(raw(read("../ex/ug-install/apply-upgrade.txt")))

That is the normal result of a replacement. The #cmd("ACCEPT") step of the job
is set to run through it.

Check the members, not the condition codes. For each module the two messages
#cmd("HMA2240") (the old module is deleted) and #cmd("HMA2380") (the new one is
copied) show that the upgrade happened.

If SMP meets an element that it does not own, it prints #cmd("NOT SEL"),
copies nothing and still ends with RC 00 and #cmd("HMA2270 ... SUCCESSFULLY COMPLETED"). That is an install that reports success and changes nothing.
Read #cmd("UFSD000I") after the restart as the last word: it names the
version and the commit of the running module. (#cmd("UFSD005I") names the
LIBC/370 it was linked with and does not tell you whether the upgrade took.)

=== From 1.3.0 <ug-install-from13>

1.3.0 (#cmd("TUFS130")) installs into #cmd("UFSD.*") already, so no name
changes, and there is nothing to allocate and no #cmd("STEPLIB") to change.
But this release deletes only #cmd("TUFS140"), so #cmd("TUFS130") still owns
#cmd("MOD(UFSD)"), and a plain install prints #cmd("NOT SEL") and ends with
RC 00 with nothing copied.

Either install 1.4.0 first and this release after it (two ordinary
upgrades), or release the inventory by hand:

+ Stop UFSD with #cmd("/P UFSD").
+ Run the UCLIN job of the fallback in the removal chapter
  (@ug-remove-uclin) and nothing else. It edits the CDS and the ACDS and
  touches no library. Do not run the dummy function (@ug-remove-dummy), which
  empties the libraries, and do not scratch them (@ug-remove-scratch).
+ Run the steps in @ug-install-upload and @ug-install-job. Start UFSD and read
  #cmd("UFSD000I"): it must name the version and the commit you just
  installed.

The hand route has not been run from 1.3.0 to 1.5.0. The UCLIN it relies on
has been measured on its own.

=== From 1.2.x <ug-install-from12>

1.2.0, 1.2.1 and 1.2.2 kept their data sets under #cmd("UFSD.V1R2M0.*"),
#cmd("UFSD.V1R2M1.*") or #cmd("UFSD.V1R2M2.*"), and all three came under the
FMID #cmd("TUFS120"). Later releases install into #cmd("UFSD.*"). The first
upgrade across the rename is therefore a move: the old data sets are not
written to, and you scratch them yourself when the new server runs.

Only 1.3.0 deletes #cmd("TUFS120"). This release leaves it as the owner of
#cmd("MOD(UFSD)"), and SMP prints #cmd("NOT SEL") for every module. Release the
inventory by hand first:

+ Stop UFSD with #cmd("/P UFSD").
+ Run the UCLIN job of the fallback in the removal chapter
  (@ug-remove-uclin) and nothing else. Do not run the dummy function
  (@ug-remove-dummy) and do not scratch the libraries (@ug-remove-scratch).
+ Run the allocation job (@ug-install-alloc). It creates #cmd("UFSD.LINKLIB")
  and #cmd("UFSD.AUFSDLOD") next to the old ones. This is the one upgrade
  that needs it.
+ Run the steps in @ug-install-upload and @ug-install-job. SMP does not
  touch the old data sets. It records the ddname through which an element was
  installed and not the data set behind it, so it resolves #cmd("LINKLIB") in
  this job, where it names the new library.
+ Change what names the library: the #cmd("STEPLIB") of your procedures
  #cmd("UFSD") and #cmd("UFSDCLNP"), client JCL that names it and, if you
  took the APF route, the entry in #cmd("SYS1.PARMLIB(IEAAPF00)"). That is one
  more IPL, and the last one, because the name does not change again.
+ Start UFSD and read #cmd("UFSD000I"). It must name the version and the
  commit you just installed. If it still names the old build, the previous
  step did not take effect: the task loads from a library you did not change.
  Do not go on until it names the new build.
+ Only then scratch the old data sets: #cmd("DELETE UFSD.V1R2Mx.LINKLIB"),
  #cmd(".AUFSDLOD") and #cmd(".SAMPLIB") with #cmd("NONVSAM SCRATCH PURGE").
  Copy first what you want out of the old SAMPLIB.

#note[The last two steps are the upgrade, not housekeeping. The old library
survives the installation, still filled, still a working load module, and
still authorised if it was before. Nothing in the job output mentions it. A
#cmd("STEPLIB") that was not changed keeps loading the old server, and the
installation that reported success from top to bottom looks as if it had
done nothing.]

An upgrade is proven by a positive statement and never by the absence of a
failure. Nothing reports the old server as still running. Only
#cmd("UFSD000I") that names the new commit is evidence. Until you scratch the
old data sets, both installations sit side by side, and a bad start is one
edit of the procedure away from being undone. See that as a window that you
chose and then close.

Each step of this route was measured on its own. The sequence as a whole has
not been run from 1.2.x to 1.5.0. The alternative is to walk through the
releases in order, 1.3.0, 1.4.0 and then this one, each an ordinary upgrade
after the first.

=== From a System Without SMP <ug-install-fromnosmp>

Before 1.2.0, UFSD was installed with a plain TSO RECEIVE into a load library
of the site's choice, and SMP knew nothing about it. The check at the top of
this section finds nothing, and there is nothing to release. The route that
worked is a new SMP installation next to the old one, and then a switch:

+ Do a new installation with the steps in @ug-install-upload to
  @ug-install-job, the allocation job included. The old load library stays as
  it is.
+ Change what names the library: the #cmd("STEPLIB") of your procedures,
  client JCL and, on the APF route, the APF entry (one IPL).
+ Stop UFSD, start it, and read #cmd("UFSD000I"). It must name the version and
  the commit you just installed.
+ Only then scratch the old load library.

Your #cmd("ROOT") and #cmd("MOUNT") data sets and your PARMLIB member are not
touched by any of this.

The install job writes #cmd("UFSD.LINKLIB"), #cmd("UFSD.AUFSDLOD"),
#cmd("UFSD.UFSDLOAD") and #cmd("UFSD.SAMPLIB"). If your old installation uses
one of these names, stop first. The allocation job fails on an existing
#cmd("UFSD.LINKLIB") or #cmd("UFSD.AUFSDLOD"), and the step #cmd("DELOLD") of
the install job scratches #cmd("UFSD.UFSDLOAD") and #cmd("UFSD.SAMPLIB").
Copy what you want out of them, or move the old library to another name.
That case has not been run.

== When Something Goes Wrong <ug-install-trouble>

#idx("troubleshooting", "installation")
#tab(caption: [Problems during installation and the first start])[
  #table(columns: (2.2in, 1fr),
    [Symptom], [Likely cause],
    [The allocation job fails because the data set exists.], [It was
      run before. Do not force it (@ug-install-alloc).],
    [#cmd("RECV1") or #cmd("RECV2") fails because the target exists.],
      [Something else allocated it. RECEIVE refuses to merge. Scratch the
      data set and run the job again.],
    [#cmd("APPLYCHK") ends non-zero and #cmd("APPLY") is skipped.], [Read
      the SMP output. The check exists to stop before anything is written.
      A missing DD statement is the usual cause.],
    [SMP reports success, but the module is not there.], [A ddname says
      nothing about the data set. Check the JCL, then look at the library
      itself (@ug-install-verify).],
    [S806 at #cmd("/S UFSD").], [The #cmd("STEPLIB") of the procedure does not
      name the library that the #cmd("APPLY") wrote to.],
    [#cmd("/S UFSD") is rejected, procedure not found.], [The procedure
      is not in a library of the started-task concatenation.],
    [#cmd("UFSD091E APF SETUP FAILED").], [There is no RAKF, so no SVC 244,
      and there is no APF entry (@ug-install-auth).],
    [S0C4 right after #cmd("UFSD047I"), only with an APF-authorised library.],
      [Release 1.2.0 or earlier. Upgrade, or drop the APF entry and let SVC
      244 do it.],
    [#cmd("UFSD100W"), then #cmd("UFSD061E PARMLIB (DD:UFSDPRM) UNUSABLE -- SHUTTING DOWN").],
      [The member cannot be opened. #cmd("D=") or #cmd("M=") is wrong, or the
      member is in a PARMLIB that the procedure does not name. On TK5 it
      is usually #cmd("SYS2.PARMLIB") that does not exist
      (@ug-install-parmlib). The text of #cmd("UFSD100W") says
      #cmd("USING DEFAULTS"), but there are none: the start ends with RC 8.],
    [#cmd("UFSD106E") instead of #cmd("UFSD100W").], [The member was found, but
      it could not be read to the end. This is a medium or DCB problem, not a
      question of configuration.],
    [#cmd("UFSD105W PARMLIB: ROOT STATEMENT MISSING") and #cmd("UFSD061E").], [The member has
      no #cmd("ROOT") statement.],
    [#cmd("UFSD120E") for the root or a mount.], [The data set cannot be
      allocated. It does not exist, or it is in use. Create it first
      (@ug-disks).],
    [#cmd("UFSD044E ddname: NOT A VALID UFS DISK (TYPE=xxxx)").], [The data
      set exists, but its first block is not that of a UFS file system.
      It was not formatted with UFSFMT.],
    [#cmd("UFSD062E") or #cmd("UFSD124E").], [A check of the superblock
      failed. The data set was not formatted with UFSFMT, or it is damaged.],
    [S106 at the start on a new library.], [The XMIT was sent in text mode.
      Send it again in binary and run the install job again.],
  )
]

The meaning of every message, with the action, is in the appendix of the
_UFSD Reference_.
