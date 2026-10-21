#import "../bookmaster/bookmaster.typ": *

= Programming with libufs <ug-libufs>

#idx("libufs")
#idx("client library")
This chapter is for the programmer who wants a C or assembler program of their
own to use the file systems of UFSD (ufsd): to create a directory, write a
file, read it back and list what is there. It takes you from the library file
to a program that runs as a batch job, and it says what to do when a call
fails. Every function in full, with its parameters, its return value and its
return codes, is in the _UFSD Reference_.

The client library is called #cmd("libufs"). A program that is linked with it
does not touch a disk. It sends each request to the started task UFSD, as
described in @ug-intro-how, and UFSD does the work.

The programs in this chapter are real. They were built on a host with CC/370
(cc370), LIBC/370 (libc370) 2.25.1 and MBT (mbt) 3, and they ran as batch
jobs against UFSD 1.5.0 on TK5 in a test system. The output shown is what
they printed. Nothing else was tried: where the text says that something was
not run, it was not run.

== What You Need <ug-libufs-need>

#idx("libufs", "prerequisites")
- *A running UFSD* with at least one file system that you may write to. The
  examples use #cmd("/tmp"), the read/write mount of the sample member
  #cmd("UFSDPRM0") (@ug-config). If you mount nothing else, UFSD is ready when
  #cmd("UFSD001I") has appeared (@ug-ops-start).
- *Authority.* UFSD refuses a caller that is not authorised. The programs in
  this chapter ask for it themselves with #cmd("clib_apf_setup"), which uses the
  SVC 244 of RAKF (@ug-libufs-auth).
- *A build host* with CC/370 and LIBC/370, and a way to bring a load module to
  MVS. The examples use MBT, which also fetches the library (@ug-libufs-get).

== Getting the Library <ug-libufs-get>

#idx("libufs", "package")
#idx("ufsd-1.5.0-lib.tar.gz")
The library is not part of the installation package. It is the file
#cmd("ufsd-1.5.0-lib.tar.gz") of the release (@ug-intro-package). Its
checksum is in the #cmd("SHA256SUMS") of the release. It holds three files:

#tab(caption: [The contents of ufsd-1.5.0-lib.tar.gz])[
  #table(columns: (2.2in, 1fr),
    [File], [What it is],
    [#cmd("ufsd-1.5.0/include/libufs.h")], [The declarations of all functions and
      the structures that your program receives.],
    [#cmd("ufsd-1.5.0/include/ufsdrc.h")], [The return codes, as
      #cmd("UFSD_RC_*"). #cmd("libufs.h") includes it.],
    [#cmd("ufsd-1.5.0/lib/libufs.a")], [The library, an archive for the
      linkage editor of CC/370.],
  )
]

With MBT, you do not unpack the archive. A project names UFSD as a dependency,
and #cmd("mbt deps") fetches the archive of the release, checks its checksum
against #cmd("mbt.lock") and stages the headers and the library. The modules
of the project are linked with #cmd("libufs.a") without further
configuration. This is the complete project file of the examples:

#code(read("../ex/ug-libufs/mbt.toml"))

#v(0.6em)
The line #cmd("libc370 = \"2.24.1\"") names the lowest LIBC/370 that MBT
accepts for the project; the build used 2.25.1, the version on the host.
Other versions were not tried. #cmd("rent = false") and
#cmd("reus = false") make the programs plain load modules that run without
the reentrant and reusable attributes. #cmd("libufs") keeps no state of its own
outside the handles that you hold, but this chapter did not test running it
reentrant.

Then fetch the dependency. #cmd("mbt deps") needs no further configuration:

#screen(```
mbt deps
```)

#screen(raw(read("../ex/ug-libufs/deps.txt")))

You can also build without MBT: put the #cmd("include") directory on the
include path and give the linkage editor the archive. Only the MBT route was
run for this book.

Take the library of the same release as the UFSD that runs. The request
format between #cmd("libufs") and UFSD is not a documented interface, and a
library of one release against a server of another was not tried.

== A First Program <ug-libufs-first>

#idx("libufs", "first program")
The program below does what most programs with files do. It opens a session,
creates a directory, writes a file, reads it back, lists the directory, asks
for the data of the file, makes one call fail on purpose to show the error,
and cleans up. The directory #cmd("/tmp/ufsdemo") does not exist before and
does not exist after.

#code(read("../ex/ug-libufs/ufsdemo.c"), numbers: true)

=== Build and Deploy <ug-libufs-build>

Build the project, then send the load modules to MVS:

#screen(raw(read("../ex/ug-libufs/build.txt")))

#screen(```
mbt deploy --target env --reallocate
```)

#screen(raw(read("../ex/ug-libufs/deploy.txt")))

The project file also lists the two assembler modules of @ug-libufs-asm,
which is why the deploy sends three modules; the build output above shows the
first program only. #cmd("env") is the name of the target in the #cmd("targets.toml") of the build
host; use your own. The library #cmd("UFSDEMO.DEV.LINKLIB") is a data set of
the test system. #cmd("--reallocate") deleted it and allocated it again;
leave it out on a system where the library holds other programs. By MBT
convention a development library is not the library of a product, and it has
no place on a production system.

=== Run the Program <ug-libufs-run>

The program is a batch step. The #cmd("STEPLIB") names the library that
#cmd("mbt deploy") filled. It need not be an APF-authorised library; the
program asks for authority itself.

#screen(raw(read("../ex/ug-libufs/run.jcl")))

The step ended with the condition code 0, and the output of the program, in
the #cmd("SYSPRINT") data set of the job, is:

#screen(raw(read("../ex/ug-libufs/run.txt")))

The two lines of the directory with the names #cmd(".") and #cmd("..") are
part of every listing. The size of 128 bytes of a directory is the size of its
two entries. The owner of #cmd("hello.txt") is empty because a file in
#cmd("/tmp") is created without one (@ug-libufs-auth).

=== What the Program Does <ug-libufs-steps>

#idx("libufs", "session")
#tab(caption: [The steps of the first program])[
  #table(columns: (1.9in, 1fr),
    [Call], [What it does and what to check],
    [#cmd("clib_apf_setup(argv[0])")], [Makes the task authorised. It returns 0
      on success. The program stops with a message if it does not.],
    [#cmd("ufsnew()")], [Opens a session with UFSD and returns the handle that
      every later call takes, or NULL (@ug-libufs-errors).],
    [#cmd("ufs_mkdir")], [Creates the directory. It returns 0, or a return code.
      The program accepts #cmd("UFSD_RC_EXIST"), a directory left by an earlier
      run, and takes any other code as failure.],
    [#cmd("ufs_fopen(...\"w\")")], [Creates the file, or empties an existing one,
      and opens it for writing. It returns NULL if it fails, and
      #cmd("ufs_last_rc") tells why.],
    [#cmd("ufs_fputs"), #cmd("ufs_fsync"), #cmd("ufs_ferror")], [Writes two lines.
      The text is held in the program until #cmd("ufs_fsync") sends it. A
      write error is visible only through #cmd("ufs_ferror"), so the program
      asks before it closes.],
    [#cmd("ufs_fclose(&fp)")], [Closes the file, frees the handle and sets
      #cmd("fp") to NULL. It returns nothing.],
    [#cmd("ufs_fopen(...\"r\")"), #cmd("ufs_fgets")], [Opens for reading and reads
      line by line until #cmd("ufs_fgets") returns NULL.],
    [#cmd("ufs_diropen"), #cmd("ufs_dirread"), #cmd("ufs_dirclose")], [Lists the
      directory. #cmd("ufs_dirread") returns one entry after the other, then
      NULL.],
    [#cmd("ufs_stat")], [Returns the data of one path in the same structure as
      a directory entry, without opening it.],
    [#cmd("ufs_remove"), #cmd("ufs_rmdir")], [Remove the file and then the
      directory, which must be empty.],
    [#cmd("ufsfree(&ufs)")], [Closes the session and frees the handle.],
  )
]

The program calls #cmd("ufsfree") also when a step failed. A session that is
not closed holds one of the 64 sessions of UFSD until the operator releases it
with #cmd("SESSIONS PRUNE") (@ug-ops-stats). Close files and sessions on every
path out of the program.

== Handling Errors <ug-libufs-errors>

#idx("libufs", "errors")
#idx("return codes", "libufs")
The calls report failure in three different ways, and a program has to read
each in its own way:

#tab(caption: [How the calls report a failure])[
  #table(columns: (2.2in, 1fr),
    [Calls], [A failure shows as],
    [#cmd("ufs_mkdir"), #cmd("ufs_rmdir"), #cmd("ufs_remove"),
      #cmd("ufs_chgdir"), #cmd("ufs_stat"), #cmd("ufs_setuser")],
      [A return value other than 0. It is the return code of UFSD (28, 32, 68, \...),
      or -1 if the request did not reach UFSD at all.],
    [#cmd("ufs_fopen")], [NULL. The return code is in #cmd("ufs_last_rc").],
    [#cmd("ufs_fread"), #cmd("ufs_fwrite"), #cmd("ufs_fputs"),
      #cmd("ufs_fgets")], [Fewer items than asked for, or NULL, or the end-of-file
      value. #cmd("ufs_ferror") on the handle gives the cause.],
    [#cmd("ufs_diropen")], [NULL. Do not read #cmd("ufs_last_rc") (see below).],
    [#cmd("ufsnew")], [NULL, and no return code.],
  )
]

The return codes are the numbers in the table of return codes in the chapter
"Limits and Disk Format" of the _UFSD Reference_, and the
header #cmd("ufsdrc.h") names them as #cmd("UFSD_RC_*"). A program compares
with the names, never with the numbers.

#cmd("ufs_last_rc(ufs)") returns the code of the last call that sets it. The
calls of the first two rows of the table above set it on success as well as on
failure, and so does #cmd("ufs_stat") when the request was sent. The calls on
a file handle never set it; they use #cmd("ufs_ferror"), and a call that is
refused before it is sent (for example a path that is too long for
#cmd("ufs_fopen")) leaves it as it was. Read it right after the call. #cmd("ufs_diropen") does not set it. After a failed #cmd("ufs_diropen") of a
directory that does not exist, #cmd("ufs_last_rc") still held 32 from an
earlier #cmd("ufs_mkdir"), and a program that reports that code names the
wrong error. To tell a missing directory from other failures, call
#cmd("ufs_stat") on the path first.

#screen(raw(read("../ex/ug-libufs/pit.txt")))

Lines 2 to 4 of this output are the case: the existing directory gives 32,
the missing directory gives NULL, and the code is still 32. The output is the
run of the program #cmd("ufspit.c") of the examples of this book. The other
lines are explained in @ug-libufs-stdio.

=== When ufsnew Returns NULL <ug-libufs-newnull>

#cmd("ufsnew") returns no return code. #cmd("errno") shows the two common causes:

#tab(caption: [The causes of a NULL from ufsnew])[
  #table(columns: (1.1in, 1fr),
    [errno], [Cause],
    [1 (EPERM)], [The task is not authorised. The program did not call
      #cmd("clib_apf_setup"), or the call failed. The job ended with the
      condition code 12.],
    [0], [UFSD is not running (or is stopped), or all 64 sessions are in use.],
  )
]

The first case, measured with a program that does not call
#cmd("clib_apf_setup"):

#screen(raw(read("../ex/ug-libufs/noapf.txt")))

The second, with the authority set and UFSD stopped with #cmd("P UFSD"):

#screen(raw(read("../ex/ug-libufs/down.txt")))

The two cases look different only through #cmd("errno"), and #cmd("errno") of 0
does not tell a stopped UFSD from a full session table. Look at the console
(@ug-ops-stats) to decide. The EPERM from an unauthorised caller is what
LIBC/370 2.23.0 and later do; earlier releases ended the step with a system
abend S047 instead. The build in this chapter used 2.25.1.

== Authority and Permissions <ug-libufs-auth>

#idx("libufs", "authority")
#idx("clib_apf_setup")
Three questions are separate: whether UFSD talks to the program, whether it
may write to the file system, and who UFSD thinks the program is.

*Whether UFSD talks to the program.* UFSD serves only authorised callers.
The call is #cmd("clib_apf_setup") of the header #cmd("<mvs/apf.h>"), with the
program name as the argument, as the first thing in #cmd("main"):

#screen(```
#include <mvs/apf.h>
...
if (clib_apf_setup(argv[0]) != 0) {
    printf("not authorized: cannot call UFSD\n");
    return 12;
}
```)

It uses the SVC 244 of RAKF, which TK4- and TK5 have. The programs of this
chapter ran this way, from a #cmd("STEPLIB") that is not in the APF list.
On a system without RAKF the load library of the program has to be in the APF
list instead (@ug-install-auth). That was not tried.

*Whether it may write.* The write check is that of @ug-config-access, and a
program meets it as a return code. With the mounts of the test system, and a
session that has just been opened:

#screen(raw(read("../ex/ug-libufs/perm.txt")))

The root is mounted read only, so creating #cmd("/newdir") fails with 68
(ROFS). #cmd("/u/herc01") is mounted with #cmd("OWNER=HERC01"), and the
session has no user ID yet, so the same call gets 72 (EACCES). Reading is never
refused.

*Who UFSD thinks the program is.* A new session has no user ID. Contrary to
what one might expect, it does not take the user ID of the job. The program
names one with #cmd("ufs_setuser(ufs, \"HERC01\", NULL)"), and from then on the
write check compares that name with the #cmd("OWNER") of the mount. The third
and fourth line of the output above show the effect: after the call the same
#cmd("ufs_mkdir") returns 0.

#cmd("ufs_setuser") does not check the name. The fifth line shows it
accepting #cmd("MALORY"), which is not a user of the system, with the return
code 0. The sixth shows that the owner check then does its job and refuses the
write. The consequence: the #cmd("OWNER") of a mount keeps a program that does
not name that user out, and it does not keep out a program that does. This is
a protection against mistakes, not against an attacker, and UFSD's other
checks do not make up for it (@ug-intro-limits).

Files and directories in #cmd("/tmp") are created with an empty owner.
Created objects get fixed permission bits (#cmd("rw-r--r--") for a file,
#cmd("rwxr-xr-x") for a directory). #cmd("ufs_set_create_perm") changes a value
in the handle that is never sent to UFSD.

== Where libufs Differs from stdio <ug-libufs-stdio>

#idx("libufs", "stdio")
The names of the file functions look like those of the C library, and the
differences are the ones a programmer finds by experience. The table lists the
ones that were measured in the output of @ug-libufs-errors (the program
#cmd("ufspit.c")) and the ones that follow from the source of the library.

#tab(caption: [Differences between libufs and stdio])[
  #table(columns: (1.5in, 1fr),
    [Subject], [What libufs does],
    [Mode of #cmd("ufs_fopen")], [Only a first character of #cmd("r") or
      #cmd("R") opens for reading. Every other mode opens for writing and
      creates the file or empties it. #cmd("\"r+\"") and #cmd("\"w+\"") do not
      give a handle for both: a handle is for reading or for writing.],
    [Mode #cmd("\"a\"")], [Is not append. The file was written with "first" and
      a new line, then opened with #cmd("\"a\""), and "second" and a new line
      written: the file held 7 bytes, the second line only. Write the whole
      file again, or keep your own copy.],
    [Wrong direction], [#cmd("ufs_fwrite") on a read handle and
      #cmd("ufs_fread") on a write handle return 0, and #cmd("ufs_ferror")
      gives 20 (INVALID).],
    [#cmd("ufs_fseek")], [Always returns -1 and does nothing. There is no
      #cmd("ftell"). A file is read from the beginning to the end. To read it
      again, close it and open it again.],
    [#cmd("ufs_feof")], [Becomes true when the read-ahead of 4096 bytes reaches
      the end of the file, while lines are still held in the buffer. A loop
      #cmd("while (!ufs_feof(fp))") read 1 of 3 lines of a short file. Use
      #cmd("while (ufs_fgets(...))") as the test.],
    [#cmd("ufs_fgets") and #cmd("ufs_fread")], [#cmd("ufs_fgets") and
      #cmd("ufs_fgetc") read ahead 4096 bytes and keep them in the handle;
      #cmd("ufs_fread") does not look there. After a #cmd("ufs_fgets"), a
      #cmd("ufs_fread") of 4 bytes of a short file returned 0. Do not mix the
      two on one handle.],
    [#cmd("ufs_fputs") and #cmd("ufs_fwrite")], [#cmd("ufs_fputs") and
      #cmd("ufs_fputc") collect the text in the handle (4096 bytes) and send it
      when the buffer is full, at #cmd("ufs_fsync") and at #cmd("ufs_fclose").
      #cmd("ufs_fwrite") sends at once and does not flush the collected text
      first. "AAA" by #cmd("ufs_fputs"), "BBB" by #cmd("ufs_fwrite") and "CCC"
      by #cmd("ufs_fputs") gave a file with "BBBAAACCC". Do not mix the two on
      one handle.],
    [Errors at close], [#cmd("ufs_fclose") ignores a failure of the last send.
      Call #cmd("ufs_fsync") and look at #cmd("ufs_ferror") before it, as the
      first program does.],
    [#cmd("ufs_sync")], [Does nothing.],
    [#cmd("ufs_diropen")], [The pattern (the third argument) is ignored. A
      directory with three #cmd(".txt") files and one #cmd(".dat") file listed
      six entries for the pattern #cmd("*.txt"). The entries #cmd(".") and
      #cmd("..") are always listed. The list is read when the directory is
      opened, sorted by name (in the EBCDIC order, small letters before
      capitals before digits) and a later change is not seen.],
    [Relative paths], [Work. After #cmd("ufs_chgdir") a path without a leading
      slash starts in that directory. #cmd("ufs_get_cwd") returns the text
      that you gave to #cmd("ufs_chgdir"), as you gave it, and not a clean
      path.],
    [Long paths], [A path longer than 255 characters is cut by
      #cmd("ufs_mkdir"), #cmd("ufs_rmdir"), #cmd("ufs_remove") and
      #cmd("ufs_chgdir") without a message. #cmd("ufs_fopen") takes 251, and
      #cmd("ufs_stat") and #cmd("ufs_diropen") refuse a path of 256 or more.],
  )
]

== Limits to Plan For <ug-libufs-limits>

#idx("libufs", "limits")
The limits of UFSD are in the chapter "Limits and Disk Format" of the _UFSD
Reference_. These are the
ones a program meets, with what was measured on the test system:

#tab(caption: [The limits a program meets])[
  #table(columns: (1.8in, 1fr),
    [Limit], [What happens],
    [Length of a name: 59], [A name of 59 characters works. One of 60 is not
      found, and #cmd("ufs_fopen") returns NULL with the code 28 (NOFILE), also
      when it should create the file.],
    [Sessions: 64], [The 65th #cmd("ufsnew") returns NULL.],
    [Open files in a session: 64], [The 65th #cmd("ufs_fopen") returns NULL with
      the code 4 (NOREQ).],
    [Memory of the program], [A handle of an open file takes about 8 KB of the
      heap of the program: the buffers for reading and for writing. In a batch
      step with the default region of the test system only 37 files could be
      opened at the same time, and #cmd("ufs_fopen") returned NULL with the
      code 52 (IO). With #cmd("REGION=8M") the limit was the 64 files of the
      session. Close files you no longer use, and give the step a region for
      the number of files that it holds.],
    [Size of a file: 4 259 840 bytes], [On a disk with 4096-byte blocks.
      #cmd("ufs_fwrite") stops at the limit, and #cmd("ufs_ferror") gives 44
      (NOSPACE), also when the disk is not full.],
    [Inodes], [A file system holds the number of files and directories that it was
      formatted for. The next #cmd("ufs_fopen") or #cmd("ufs_mkdir") gets 48
      (NOINODES). On the file system of the test system, 125 files were
      created in an empty directory, and the 126th failed with 48.],
  )
]

#note[*A defect* (ufsd\#130): inodes of removed files and directories are not
all returned to the file system. Once more than 64 have been removed since
the start, the others are lost for good, and #cmd("F UFSD,REBUILD") does not
find them. On the test system a program that created 125 files in an empty
directory and then removed all of them left the file system with no free
inode: every later create failed with 48. A program that creates and removes
many objects in one file system should not rely on the free space coming
back.]

== Using libufs from Assembler <ug-libufs-asm>

#idx("libufs", "assembler")
#idx("assembler", "calling libufs")
libufs is a C library, and the functions follow the linkage of CC/370. An
assembler program calls them in one of two ways. Both were built and run as
batch jobs, and a third way is shown because it fails.

The names to call are the external names of the library, not the C names. A
function that the header declares with #cmd("asm(\"...\")") has exactly that
name, for example #cmd("UFS#MD") for #cmd("ufs_mkdir") and #cmd("UFS#FOPN") for
#cmd("ufs_fopen"). #cmd("ufsnew") and #cmd("ufsfree") have none; CC/370 makes
#cmd("UFSNEW") and #cmd("UFSFREE") of them. The table of all names is in the
Reference.

=== An Assembler Routine Called from C <ug-libufs-asma>

The simple case is a C program that has the #cmd("main") and calls an
assembler routine, which calls libufs. The routine follows the C linkage of
CC/370 (cc370): it is called with register 1 pointing to a
list of parameter values, register 13 pointing to the frame of the caller, and
the word at offset 76 of that frame holding the address of the next free
storage of the C stack. It takes a frame from there. When it calls a libufs
function, it builds the same kind of list: a list of the parameter
#emph[values], not of their addresses. A pointer parameter such as
#cmd("ufs_fclose(UFSFILE **)") gets the address of a word that holds the file.

The routine below writes a text to a file. The C program, the second listing,
opens the session, calls it, reads the file back with libufs and removes it.

#code(read("../ex/ug-libufs/asmput.asm"), size: 7.5pt)

#v(0.8em)
#code(read("../ex/ug-libufs/ufsasma.c"), size: 7.5pt)

#v(0.8em)
The module has two sources, as the project file shows. It ended with the
condition code 0 and printed:

#screen(raw(read("../ex/ug-libufs/asma.txt")))

=== An Assembler Program Without a C Main Program <ug-libufs-asmb>

This works too. A program that is entirely assembler follows the usual OS
linkage: its own save area in register 13 and register 1 pointing to a list of
addresses. It cannot call C functions directly, because their linkage differs.
The macro #cmd("CCALL") of LIBC/370 makes a stub that calls a C function from
OS linkage. The stub branches to a routine of the C runtime that builds the C
environment at the first call; at the end the program calls #cmd("CCTERM")
to end it again. Each stub is a CSECT of its own that you name:

#screen(```
CUFSNEW  CCALL UFSNEW
CMKDIR   CCALL UFS#MD
```)

The program calls the stub with #cmd("L 15,=V(CMKDIR)") and #cmd("BALR 14,15"),
with register 1 pointing to a list of addresses of the parameters in which
the last has its high bit set. The functions of the C runtime that the program
needs, such as #cmd("clib_apf_setup") (#cmd("@@APFSET")) and #cmd("puts"), are
called the same way. The return value comes back in register 15.

Link the program without the C startup and name its entry. In the project
file:

#screen(```
[module.UFSASMB]
sources = ["asm/ufsasmb.asm"]
startup = false
entry = "UFSASMB"
```)

The program asks for authority, opens a session, creates a directory and a
file, writes a line, reads it back, prints it with #cmd("puts"), removes the
file and the directory, closes the session and ends the C environment:

#code(read("../ex/ug-libufs/ufsasmb.asm"), size: 7.5pt)

#v(0.8em)
It ended with the condition code 0 and printed:

#screen(raw(read("../ex/ug-libufs/asmb.txt")))

The program writes the line without a line end, so that #cmd("puts") makes
one. If the authority or the session fails it ends with the code 12, and if
the file cannot be opened with 8, in both cases after a message. Otherwise it
ends with 0. It does not check the return value of #cmd("ufs_mkdir"); a
program of your own should.

=== What Does Not Work <ug-libufs-asmno>

Calling a libufs function from OS linkage without the stub does not work.
The program below calls #cmd("UFSNEW") directly, as one would call a
subroutine, with a save area and no C environment:

#code(read("../ex/ug-libufs/ufsasmc.asm"), size: 7.5pt)

#v(0.8em)
The linkage editor resolved the name, and the step ended with a system abend:

#screen(raw(read("../ex/ug-libufs/asmc.txt")))

The functions need the C stack and the C heap: #cmd("ufsnew"), #cmd("ufs_fopen")
and #cmd("ufs_diropen") allocate memory. A routine that does not set the
environment up or pass the calls through #cmd("CCALL") has none. These
other ways were not built, and the book makes no claim about them: an
assembler #cmd("main") entered by the C startup, a program in another language,
and a program in an environment other than a batch job.

This book does not describe C linkage or the C environment in general; see
the documentation of CC/370 (cc370) and LIBC/370 (libc370).

== When Something Goes Wrong <ug-libufs-trouble>

#idx("libufs", "troubleshooting")
#tab(caption: [Symptoms of a libufs program])[
  #table(columns: (2.0in, 1fr),
    [Symptom], [Cause and cure],
    [The step ends with condition code 12 and #cmd("ufsnew") returned NULL,
      #cmd("errno") 1], [The program is not authorised. Call
      #cmd("clib_apf_setup") first (@ug-libufs-auth).],
    [#cmd("ufsnew") returned NULL, #cmd("errno") 0], [UFSD is stopped or has
      no free session. Look at the console and at #cmd("F UFSD,SESSIONS")
      (@ug-ops-stats).],
    [S806 when the step starts], [The load module is not found. Check the
      #cmd("STEPLIB") and that the module is in the library
      (@ug-install-trouble).],
    [#cmd("ufs_mkdir") or #cmd("ufs_fopen") gives 68], [The file system is
      read only. Choose a mount that is read/write (@ug-ops-mount).],
    [The same gives 72], [The mount has an owner, and the session has not been
      set to it (@ug-libufs-auth).],
    [#cmd("ufs_fopen") gives 28 when it creates a file], [A directory on the
      path does not exist, or the name is longer than 59 characters.],
    [#cmd("ufs_fopen") gives 52 and there are many files open], [The heap of
      the program is exhausted (@ug-libufs-limits).],
    [#cmd("ufs_fopen") or #cmd("ufs_mkdir") gives 48 on an empty file system],
      [The inodes are used up (@ug-libufs-limits).],
    [#cmd("ufs_ferror") gives 44], [The file reached 4 259 840 bytes, or the
      disk is full.],
    [A file holds only the last write, or text in the wrong order], [The mode
      #cmd("\"a\"") empties the file, and the text of #cmd("ufs_fputs") is sent
      after #cmd("ufs_fwrite") (@ug-libufs-stdio).],
    [S0C1 or S0C4 in an assembler program], [A libufs function was called
      without #cmd("CCALL") and the C environment (@ug-libufs-asmno).],
  )
]
