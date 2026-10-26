#import "../bookmaster/bookmaster.typ": *

= The Client Library libufs <ref-lib>

#idx("libufs")
#idx("client library")
This chapter describes the client library #cmd("libufs") of UFSD (ufsd) 1.5.2
function by function: the prototype, the parameters, the value returned and the
return codes that the function can give. How to build and run a program with it
is the subject of the _UFSD User's Guide_. The statements about behaviour in
this chapter were checked against the source of the library, and those marked
as measured were run on TK5 with the library of release 1.5.0 against the
server of the TK5 image, which is UFSD 1.4.0. The functions that were measured
are the same in the releases 1.5.1 and 1.5.2 (the library did not change
between them) except where the text names a difference
(a path of 256 characters or more, and a read answered inline in #cmd("ufs_fread"));
the server of release 1.5.2 was not run with this library.

== Conventions <ref-lib-conv>

The declarations are in #cmd("libufs.h"), which includes #cmd("ufsdrc.h"). The
return codes of UFSD are named #cmd("UFSD_RC_*"); the numbers are in
@ref-lim-rc. A code that is not 0 is a failure.

Any function that sends a request can also get four codes from the router and
the session, whatever its entry lists: #cmd("UFSD_RC_NOREQ") (no free request
block), #cmd("UFSD_RC_CORRUPT") (the control blocks of UFSD are missing or not
active), #cmd("UFSD_RC_BADSESS") (UFSD does not know the session) and
#cmd("UFSD_RC_INVALID") (the request block has no valid eye catcher). The
entries below name the codes that belong to the function and do not repeat
these four. They arrive the same way as the codes of the entry: as the return
value, in #cmd("ufs_last_rc") or in #cmd("ufs_ferror").

The library returns a failure in one of the following ways. The entry of each
function says which one it uses.

#deflist(width: 1.5in,
  [Return code], [The function returns a #cmd("UFSD_RC_*") value, 0 for success,
    or -1 if the request did not reach UFSD. The value is also stored for
    #cmd("ufs_last_rc").],
  [NULL], [The function returns a null pointer. #cmd("ufs_fopen") stores the
    code for #cmd("ufs_last_rc"); #cmd("ufsnew") does not. #cmd("ufs_diropen")
    does so only for one failure (see its entry).],
  [Handle error], [The function on an open file returns fewer items than asked
    for, NULL or #cmd("UFS_EOF"), and stores the code in the handle.
    #cmd("ufs_ferror") reads it. -1 is stored if the request did not reach
    UFSD.],
)

A handle is opaque to the program, except for the fields that the header
documents as returned data (@ref-lib-types). The library holds no state outside
the handles. It does not check for concurrent use of one handle by two tasks.

The functions need the C runtime of LIBC/370 (libc370): they allocate with
#cmd("calloc") and #cmd("malloc"), and call the subsystem interface through
#cmd("iefssreq"). The calling task must be authorised (@ref-stc-auth).

=== External Names <ref-lib-names>

#idx("libufs", "external names")
The header gives most functions an external name with #cmd("asm()"), so that
assembler programs and programs linked against ufs370 find them. #cmd("ufsnew")
and #cmd("ufsfree") have none; CC/370 (cc370) makes the names #cmd("UFSNEW") and
#cmd("UFSFREE") of them.

#tab(caption: [The external names of libufs])[
  #table(columns: (1.9in, 1.1in, 1fr),
    [Function], [Name], [Group],
    [#cmd("ufsnew")], [#cmd("UFSNEW")], [Session],
    [#cmd("ufsfree")], [#cmd("UFSFREE")], [Session],
    [#cmd("ufs_setuser")], [#cmd("UFS#SUSR")], [Session],
    [#cmd("ufs_last_rc")], [#cmd("UFS#GLRC")], [Session],
    [#cmd("ufs_chgdir")], [#cmd("UFS#CD")], [Directories],
    [#cmd("ufs_mkdir")], [#cmd("UFS#MD")], [Directories],
    [#cmd("ufs_rmdir")], [#cmd("UFS#RD")], [Directories],
    [#cmd("ufs_remove")], [#cmd("UFS#REM")], [Directories],
    [#cmd("ufs_diropen")], [#cmd("UFS#DOPN")], [Directories],
    [#cmd("ufs_dirread")], [#cmd("UFS#DRD")], [Directories],
    [#cmd("ufs_dirclose")], [#cmd("UFS#DCLS")], [Directories],
    [#cmd("ufs_stat")], [#cmd("UFS#STAT")], [Directories],
    [#cmd("ufs_fopen")], [#cmd("UFS#FOPN")], [Files],
    [#cmd("ufs_fclose")], [#cmd("UFS#FCLS")], [Files],
    [#cmd("ufs_fsync")], [#cmd("UFS#FSYN")], [Files],
    [#cmd("ufs_fread")], [#cmd("UFS#FRD")], [Files],
    [#cmd("ufs_fwrite")], [#cmd("UFS#FWT")], [Files],
    [#cmd("ufs_fgets")], [#cmd("UFS#FGTS")], [Files],
    [#cmd("ufs_fgetc")], [#cmd("UFS#FGTC")], [Files],
    [#cmd("ufs_fputs")], [#cmd("UFS#FPTS")], [Files],
    [#cmd("ufs_fputc")], [#cmd("UFS#FPTC")], [Files],
    [#cmd("ufs_feof")], [#cmd("UFS#FEOF")], [Files],
    [#cmd("ufs_ferror")], [#cmd("UFS#FERR")], [Files],
    [#cmd("ufs_clearerr")], [#cmd("UFS#CERR")], [Files],
    [#cmd("ufs_fseek")], [#cmd("UFS#FSEE")], [Files (not implemented)],
    [#cmd("ufs_sync")], [#cmd("UFS#SYNC")], [Files (does nothing)],
    [#cmd("ufs_get_cwd")], [#cmd("UFS#GCWD")], [Handle fields],
    [#cmd("ufs_set_cwd")], [#cmd("UFS#SCWD")], [Handle fields],
    [#cmd("ufs_get_acee"), #cmd("ufs_set_acee")], [#cmd("UFS#GACE"), #cmd("UFS#SACE")], [Handle fields],
    [#cmd("ufs_get_create_perm"), #cmd("ufs_set_create_perm")], [#cmd("UFS#GCRE"), #cmd("UFS#SCRE")], [Handle fields],
    [#cmd("ufs_get_sys"), #cmd("ufs_set_sys")], [#cmd("UFS#GSYS"), #cmd("UFS#SSYS")], [Stubs],
    [#cmd("ufs_sys_new"), #cmd("ufs_sys_term")], [#cmd("UFSSYNEW"), #cmd("UFSSYTRM")], [Stubs],
    [#cmd("ufs_signon"), #cmd("ufs_signoff")], [#cmd("UFS#SON"), #cmd("UFS#SOFF")], [Stubs],
  )
]

=== Types and Constants <ref-lib-types>

#tab(caption: [The types and constants of libufs.h])[
  #table(columns: (1.6in, 1fr),
    [Name], [Meaning],
    [#cmd("UFS")], [The session handle, returned by #cmd("ufsnew").],
    [#cmd("UFSFILE")], [The handle of an open file. It holds a buffer of 4096
      bytes for reading and one for writing, so it takes about 8 KB of the heap
      of the program.],
    [#cmd("UFSDDESC")], [The handle of an open directory.],
    [#cmd("UFSCWD")], [The working directory of a session; the field
      #cmd("path") has 256 characters.],
    [#cmd("UFSDLIST")], [One directory entry (below).],
    [#cmd("UFS_PATH_MAX")], [256, the size of a path buffer, the end mark
      included.],
    [#cmd("UFS_EOF")], [-1, the end-of-file value of the functions that read a
      character.],
    [#cmd("UFS_SEEK_SET"), #cmd("UFS_SEEK_CUR"), #cmd("UFS_SEEK_END")], [0, 1 and 2.
      They are declared for #cmd("ufs_fseek"), which does not use them.],
  )
]

#tab(caption: [The fields of UFSDLIST])[
  #table(columns: (1.2in, 1.1in, 1fr),
    [Field], [Type], [Contents],
    [#cmd("inode_number")], [unsigned], [The number of the inode.],
    [#cmd("filesize")], [unsigned], [The size in bytes. A directory has the
      size of its entries.],
    [#cmd("name")], [char[60]], [The name, ended by a null character. It is cut
      at 59 characters.],
    [#cmd("attr")], [char[11]], [Ten characters and a null character. The first
      is #cmd("d") for a directory or #cmd("-") for anything else (no other
      type character occurs), then #cmd("rwx") for the owner, the group and the
      others, with #cmd("-") for a missing bit.],
    [#cmd("owner")], [char[9]], [The owner, which is empty for a file that was
      created without one.],
    [#cmd("group")], [char[9]], [The group.],
    [#cmd("nlink")], [unsigned short], [The number of links.],
    [#cmd("mtime")], [mtime64_t], [The time of the last change, in milliseconds
      (type from #cmd("<ext/time64.h>") of LIBC/370).],
  )
]

== Session Functions <ref-lib-session>

=== ufsnew <ref-lib-ufsnew>

#idx("ufsnew")
#screen(```
UFS *ufsnew(void);
```)

Opens a session with UFSD and returns its handle. The session starts at the
root directory, with no user ID, and with the permission value 0755 in the
handle.

*Return value:* the handle, or NULL. There is no return code. The program
has to set #cmd("errno = 0") before the call; the rule below holds only then.
If the task is not authorised, #cmd("errno") is 1 (LIBC/370 2.19.0 and later;
earlier releases ended the step with the abend S047). If #cmd("errno") is still
0, UFSD is not running, or it has no free session (the limit is 64, @ref-lim-limits), or
the library could not allocate the handle; in the last case it closes the
session again.

=== ufsfree <ref-lib-ufsfree>

#idx("ufsfree")
#screen(```
void ufsfree(UFS **ufs);
```)

Closes the session and frees the handle. UFSD releases the files and directories
that the session still holds. The text that a file handle has collected is not
sent, and the memory of the file handles stays with the program. The pointer is
set to NULL. A NULL pointer or a
handle that is not one is ignored. A failure of the request is not reported.

=== ufs_setuser <ref-lib-setuser>

#idx("ufs_setuser")
#screen(```
int ufs_setuser(UFS *ufs, const char *userid, const char *group);
```)

Gives the session the user ID against which UFSD checks the #cmd("OWNER") of a
mount (@ref-cfg-access). Only the first 8 characters of each name are used. A
NULL or empty #cmd("group") keeps the group.

The user ID and the group also become the owner and the group of the files and
directories that the session creates afterwards. Without a call, they have an
empty owner. The comparison with the #cmd("OWNER") of the mount is *case
sensitive*: #cmd("ufs_setuser(ufs, \"mike\", NULL)") against
#cmd("OWNER(MIKE)") gets #cmd("UFSD_RC_EACCES") when the session wants to write. UFSD checks
the right to write again at every #cmd("ufs_fwrite"), not only when the file is
opened: a file that is open for writing fails with #cmd("UFSD_RC_EACCES") after
#cmd("ufs_setuser") has named another user.

*Return value:* 0, or a return code. #cmd("UFSD_RC_INVALID") if #cmd("ufs") or
#cmd("userid") is NULL, and -1 if the request did not reach UFSD; both are
returned without being stored. Any other result is stored for
#cmd("ufs_last_rc"). *The name is not checked*: any name is accepted, and a
program that names the owner of a mount passes the check of the mount.

=== ufs_last_rc <ref-lib-lastrc>

#idx("ufs_last_rc")
#screen(```
int ufs_last_rc(UFS *ufs);
```)

Returns the code that a function stored in the session handle last, or -1 if
#cmd("ufs") is NULL. The functions that store it are #cmd("ufs_chgdir"),
#cmd("ufs_mkdir"), #cmd("ufs_rmdir"), #cmd("ufs_remove"), #cmd("ufs_stat"),
#cmd("ufs_fopen"), #cmd("ufs_setuser") and, for one failure,
#cmd("ufs_diropen"). They store 0 on success. No other function stores it. A
function that fails before it sends the request stores nothing in these cases:
a NULL #cmd("ufs") (the other arguments, too, for #cmd("ufs_stat"),
#cmd("ufs_fopen") and #cmd("ufs_setuser")), an empty path or a path of 256
characters or more for #cmd("ufs_stat"), a path of 252 characters or more for
#cmd("ufs_fopen"). #cmd("ufs_setuser") stores nothing for -1 either.
#cmd("ufs_chgdir"), #cmd("ufs_mkdir"), #cmd("ufs_rmdir") and
#cmd("ufs_remove") do store #cmd("UFSD_RC_INVALID") when the library refuses
an empty path or a path of 256 characters or more.

== Directory Functions <ref-lib-dir>

A path is absolute if it begins with a slash. Otherwise UFSD takes it from the
working directory of the session. A name in a path has at most 59 characters.
A directory holds 1024 entries at the block size of 4096, the entries
#cmd(".") and #cmd("..") included; the next #cmd("ufs_fopen") that creates a
file or #cmd("ufs_mkdir") gets #cmd("UFSD_RC_NOSPACE"), also when the disk is
empty (@ref-lim-limits).

#tab(caption: [The length of a path])[
  #table(columns: (2.3in, 0.9in, 1fr),
    [Function], [Guaranteed], [A longer path],
    [#cmd("ufs_chgdir"), #cmd("ufs_mkdir"), #cmd("ufs_rmdir"),
      #cmd("ufs_remove")], [255], [Return #cmd("UFSD_RC_INVALID") before a
      request is sent; the code is stored for #cmd("ufs_last_rc"). UFSD 1.5.0
      cut a path of 256 characters or more to 255 and sent it with its 256th
      character in place of the terminating null\; UFSD 1.5.1 and later refuse it.],
    [#cmd("ufs_diropen")], [255], [Returns NULL; #cmd("ufs_last_rc") is not set.],
    [#cmd("ufs_stat")], [255], [Returns #cmd("UFSD_RC_INVALID"); the code is not
      stored for #cmd("ufs_last_rc").],
    [#cmd("ufs_fopen")], [251], [Returns NULL; #cmd("ufs_last_rc") is not set.],
  )
]

=== ufs_chgdir <ref-lib-chgdir>

#idx("ufs_chgdir")
#screen(```
int ufs_chgdir(UFS *ufs, const char *path);
```)

Changes the working directory of the session. On success the library copies
#cmd("path") as it is, not as a resolved path, into the field #cmd("path") of
#cmd("ufs_get_cwd"); after #cmd("ufs_chgdir(ufs, \"..\")") that field reads
#cmd("..").

*Return value:* 0, or a return code, or -1 (NULL #cmd("ufs") or no delivery).
#cmd("UFSD_RC_INVALID") for an empty or NULL path or one of 256 characters or more, #cmd("UFSD_RC_NOFILE") if the path does
not exist, #cmd("UFSD_RC_NOTDIR") if it is not a directory,
#cmd("UFSD_RC_IO") if the file system cannot be read.

=== ufs_mkdir <ref-lib-mkdir>

#idx("ufs_mkdir")
#screen(```
int ufs_mkdir(UFS *ufs, const char *path);
```)

Creates a directory. The parent has to exist. The directory is created with the
mode #cmd("rwxr-xr-x").

*Return value:* 0, or a return code, or -1. Codes: #cmd("UFSD_RC_EXIST"),
#cmd("UFSD_RC_NOFILE") (also if an intermediate directory is missing),
#cmd("UFSD_RC_ROFS"), #cmd("UFSD_RC_EACCES"), #cmd("UFSD_RC_NOINODES"),
#cmd("UFSD_RC_NOSPACE"), #cmd("UFSD_RC_IO") and #cmd("UFSD_RC_INVALID") for an
empty or NULL path or one of 256 characters or more.

=== ufs_rmdir <ref-lib-rmdir>

#idx("ufs_rmdir")
#screen(```
int ufs_rmdir(UFS *ufs, const char *path);
```)

Removes an empty directory.

*Return value:* 0, or a return code, or -1. Codes: #cmd("UFSD_RC_NOFILE"),
#cmd("UFSD_RC_NOTDIR"), #cmd("UFSD_RC_NOTEMPTY"), #cmd("UFSD_RC_INVALID") (the
root, an empty path, or a path of 256 characters or more), #cmd("UFSD_RC_ROFS"), #cmd("UFSD_RC_EACCES"),
#cmd("UFSD_RC_IO").

=== ufs_remove <ref-lib-remove>

#idx("ufs_remove")
#screen(```
int ufs_remove(UFS *ufs, const char *path);
```)

Removes a file. A directory is removed by #cmd("ufs_rmdir").

*Return value:* 0, or a return code, or -1. Codes: #cmd("UFSD_RC_NOFILE"),
#cmd("UFSD_RC_ISDIR"), #cmd("UFSD_RC_ROFS"), #cmd("UFSD_RC_EACCES"),
#cmd("UFSD_RC_IO"), #cmd("UFSD_RC_INVALID") for an empty path, a path of 256
characters or more, or the root.

=== ufs_diropen <ref-lib-diropen>

#idx("ufs_diropen")
#screen(```
UFSDDESC *ufs_diropen(UFS *ufs, const char *path,
                      const char *pattern);
```)

Opens a directory and reads all its entries into the memory of the program, sorted
by name with #cmd("strcmp"), the entries #cmd("."), #cmd("..") first. A change
made later is not seen by the handle. Each entry takes about 112 bytes of the
heap. #cmd("pattern") is accepted and *ignored*: all entries are returned.

The table of entries starts with room for 32 entries and doubles when it is
full. While it grows, the old and the new table exist together, which is one
and a half times the size of the new table (about 112 bytes for each entry
of it). An open directory takes one of the 64
files of the session (@ref-lim-limits).

*Return value:* the handle, or NULL if the path is 256 characters or longer, the
directory cannot be read or the program has no memory for the handle or the
entries. *#cmd("ufs_last_rc") is set only in one case*: if UFSD has accepted
the open and then the allocation or a read fails, the code is
#cmd("UFSD_RC_IO"). If UFSD refuses the open (#cmd("UFSD_RC_NOFILE"),
#cmd("UFSD_RC_NOTDIR"), #cmd("UFSD_RC_NOREQ") when the 64 files of the session
or the 256 files of the system are open), or if the client fails before the
request, the value stays as it was. To find the cause, call #cmd("ufs_stat").

=== ufs_dirread <ref-lib-dirread>

#idx("ufs_dirread")
#screen(```
UFSDLIST *ufs_dirread(UFSDDESC *ddesc);
```)

Returns the next entry, or NULL after the last. The pointer is that of one
structure in the handle that the next call overwrites; copy what you need.

=== ufs_dirclose <ref-lib-dirclose>

#idx("ufs_dirclose")
#screen(```
void ufs_dirclose(UFSDDESC **pddesc);
```)

Closes the directory, frees the entries and the handle, and sets the pointer to
NULL.

=== ufs_stat <ref-lib-stat>

#idx("ufs_stat")
#screen(```
int ufs_stat(UFS *ufs, const char *path, UFSDLIST *out);
```)

Fills #cmd("out") with the entry of a path, as #cmd("ufs_dirread") gives it.

*Return value:* 0, or a return code, or -1. #cmd("UFSD_RC_NOFILE") if the path
does not exist, #cmd("UFSD_RC_IO") if the file system cannot be read.
#cmd("UFSD_RC_INVALID") if an argument is NULL, the path is empty, or the path
is 256 characters or longer. None of these three cases stores the code for
#cmd("ufs_last_rc"), because the library refuses them before it sends a request.

== File Functions <ref-lib-file>

=== ufs_fopen <ref-lib-fopen>

#idx("ufs_fopen")
#screen(```
UFSFILE *ufs_fopen(UFS *ufs, const char *path, const char *mode);
```)

Opens a file. If the first character of #cmd("mode") is #cmd("r") or
#cmd("R"), the file is opened for reading. *Every other mode opens for writing*,
creates the file if it is missing and empties it if it exists. There is no
append mode, no mode for both directions, and the other characters of
#cmd("mode") are not read: #cmd("\"a\""), #cmd("\"w+\"") and #cmd("\"wb\"") are
the same. A file that is created has the mode #cmd("rw-r--r--"); the value of
#cmd("ufs_set_create_perm") is not sent to UFSD.

*Return value:* the handle, or NULL with the code in #cmd("ufs_last_rc"):
#cmd("UFSD_RC_ISDIR"), #cmd("UFSD_RC_NOFILE") (also if the name is longer than
59 characters, or a directory of the path is missing), #cmd("UFSD_RC_ROFS"),
#cmd("UFSD_RC_EACCES"), #cmd("UFSD_RC_NOINODES"), #cmd("UFSD_RC_NOSPACE") (no free block, or the
directory holds 1024 entries already), #cmd("UFSD_RC_NOREQ") (the session
has 64 files open, open directories included, or all sessions together have
256), #cmd("UFSD_RC_INVALID"), and #cmd("UFSD_RC_IO") if the file system cannot
be read or the program has no memory for the handle. A path of 252 characters or more,
or a NULL argument, returns NULL and leaves #cmd("ufs_last_rc") as it was.

=== ufs_fclose <ref-lib-fclose>

#idx("ufs_fclose")
#screen(```
void ufs_fclose(UFSFILE **file);
```)

Sends the text that #cmd("ufs_fputs") and #cmd("ufs_fputc") have collected,
closes the file, frees the handle and sets the pointer to NULL. A failure of
the send or of the close is *not reported*. To learn about it, call
#cmd("ufs_fsync") and #cmd("ufs_ferror") before. A NULL pointer, a pointer to
NULL, or a handle without the eye catcher of a file handle is ignored without a
message, and the pointer keeps its value.

=== ufs_fsync <ref-lib-fsync>

#idx("ufs_fsync")
#screen(```
void ufs_fsync(UFSFILE *file);
```)

Sends the collected text of #cmd("ufs_fputs") and #cmd("ufs_fputc"). It does
not ask UFSD to write blocks to the disk, and it returns nothing: a failure
shows in #cmd("ufs_ferror").

=== ufs_fread <ref-lib-fread>

#idx("ufs_fread")
#screen(```
UINT32 ufs_fread(void *ptr, UINT32 size, UINT32 nitems, UFSFILE *fp);
```)

Reads up to #cmd("size × nitems") bytes. The library asks UFSD in pieces of up
to 4096 bytes. If the common storage has no free buffer for such a piece, UFSD
answers with at most 252 bytes inline and the library asks for the rest in the
next piece. The library of release 1.5.0 took that short answer for the end of
the file: it returned a short count and set the end-of-file flag although the
file went on. Releases 1.5.1 and 1.5.2 do not. This was checked against the source of
the library, not run.

*Return value:* the number of whole items, which is smaller than #cmd("nitems")
at the end of the file and on an error, and 0 for a NULL argument, a size of 0,
or a handle with the end-of-file or error flag set. An incomplete last item is
consumed from the file but not counted: a file of 10 bytes, read with a size of 4
and 3 items asked for, gives 2 items, and the last 2 bytes are gone. At the end of the file the
flag #cmd("ufs_feof") is set; on an error the code is stored for
#cmd("ufs_ferror"): #cmd("UFSD_RC_BADFD"), #cmd("UFSD_RC_INVALID") (also if
the file was opened for writing) and #cmd("UFSD_RC_IO"). Reading a file that was
opened for writing sets the error flag, and it stays set until
#cmd("ufs_clearerr") (@ref-lib-feof).

*Do not mix with #cmd("ufs_fgets") or #cmd("ufs_fgetc") on one handle:* they
read ahead 4096 bytes into the handle, which #cmd("ufs_fread") does not look at.

=== ufs_fwrite <ref-lib-fwrite>

#idx("ufs_fwrite")
#screen(```
UINT32 ufs_fwrite(void *ptr, UINT32 size, UINT32 nitems, UFSFILE *fp);
```)

Writes #cmd("size × nitems") bytes, in pieces of up to 4096 bytes (of 248 bytes
inline, if the common storage has no free buffer). It sends at once and does
*not* first send the text that #cmd("ufs_fputs") and #cmd("ufs_fputc") have
collected, so mixing the two on one handle changes the order of the text in the
file.

*Return value:* the number of whole items written. A short count means an
error, stored for #cmd("ufs_ferror"): #cmd("UFSD_RC_NOSPACE") (the disk is full,
or the file is at its largest size, @ref-lim-file), #cmd("UFSD_RC_BADFD"),
#cmd("UFSD_RC_INVALID") (also if the file was opened for reading),
#cmd("UFSD_RC_IO"), #cmd("UFSD_RC_ROFS") and #cmd("UFSD_RC_EACCES"). The last two
come from the check of the right to write, which UFSD makes at every call, so
they also stop a write in the middle of a file that was opened with success
(@ref-lib-setuser). The items that were written before the failure are
counted. 0 for a NULL argument, a size of 0, or a handle with the error flag
set.

=== ufs_fgets <ref-lib-fgets>

#idx("ufs_fgets")
#screen(```
char *ufs_fgets(char *str, int num, UFSFILE *fp);
```)

Reads a line of at most #cmd("num - 1") characters, including the line end
character, and ends it with a null character. It reads from the buffer of 4096
bytes that it fills from UFSD.

*Return value:* #cmd("str"), or NULL if nothing could be read: at the end of the
file, on an error, or for #cmd("num") of 0 or less. A last line without a
line end is returned.

=== ufs_fgetc <ref-lib-fgetc>

#idx("ufs_fgetc")
#screen(```
INT32 ufs_fgetc(UFSFILE *file);
```)

Returns the next byte as a value from 0 to 255, or #cmd("UFS_EOF") at the end of
the file or on an error.

=== ufs_fputs and ufs_fputc <ref-lib-fputs>

#idx("ufs_fputs")
#idx("ufs_fputc")
#screen(```
INT32 ufs_fputs(const char *str, UFSFILE *file);
INT32 ufs_fputc(INT32 c, UFSFILE *file);
```)

Collect text in a buffer of the handle and send it when the buffer holds 4096
bytes, at #cmd("ufs_fsync") and at #cmd("ufs_fclose"). #cmd("ufs_fputs") writes
the string without its null character and adds no line end.

*Return value:* #cmd("ufs_fputs") returns the length of the string.
#cmd("ufs_fputc") returns the byte. Both return #cmd("UFS_EOF") for a NULL
argument, a handle with the error flag set, or if sending a full buffer failed.
A handle that was opened for reading fails at the first send; the text is lost.
*A partial write is reported as success.* If UFSD stores only a part of the
buffer, for example because the disk is full, the library empties the buffer
all the same and the rest is lost. The calls then return the length or the byte
as if all had been written. #cmd("ufs_ferror") shows the failure if UFSD
answered with a code; call it after #cmd("ufs_fsync").

=== ufs_feof, ufs_ferror and ufs_clearerr <ref-lib-feof>

#idx("ufs_feof")
#idx("ufs_ferror")
#idx("ufs_clearerr")
#screen(```
INT32 ufs_feof(UFSFILE *file);
INT32 ufs_ferror(UFSFILE *file);
void  ufs_clearerr(UFSFILE *file);
```)

#cmd("ufs_feof") returns 1 if the end of the file has been reached by the
request to UFSD, and 0 otherwise. This happens when the last block is read ahead,
not when the program has consumed the last byte; lines may still be in the
buffer. Use the return value of #cmd("ufs_fgets") or #cmd("ufs_fgetc") as the
test of the end. #cmd("ufs_ferror") returns the code of the last error of the
handle (never 0 then), or 0. Both return 1 for a NULL handle.
#cmd("ufs_clearerr") resets both flags. After the end of the file or an
error, #cmd("ufs_fread") returns 0 until the flags are cleared, and after an
error #cmd("ufs_fwrite") does so. A handle that is used in the other
direction (a read on a file opened for writing, or a write on a file opened for
reading) gets the error #cmd("UFSD_RC_INVALID") from UFSD. The error flag
stays set until the program calls #cmd("ufs_clearerr").

=== ufs_fseek and ufs_sync <ref-lib-fseek>

#idx("ufs_fseek")
#idx("ufs_sync")
#screen(```
INT32 ufs_fseek(UFSFILE *fp, INT32 offset, INT32 whence);
void  ufs_sync(UFS *ufs);
```)

#cmd("ufs_fseek") always returns -1 and does nothing. There is no function that
returns the position. #cmd("ufs_sync") does nothing.

== Handle Fields and Stubs <ref-lib-fields>

These functions read and write fields of the handle in the memory of the
program. They send nothing to UFSD.

#tab(caption: [The handle functions])[
  #table(columns: (2.3in, 1fr),
    [Function], [Effect],
    [#cmd("UFSCWD *ufs_get_cwd(UFS *)")], [Returns the #cmd("UFSCWD") of the
      handle: the text that the last successful #cmd("ufs_chgdir") was given,
      or #cmd("/"). NULL for a NULL handle.],
    [#cmd("UFSCWD *ufs_set_cwd(UFS *, UFSCWD *)")], [Copies the text into the
      handle only. UFSD does not change its directory; use #cmd("ufs_chgdir").],
    [#cmd("UINT32 ufs_get_create_perm(UFS *)"), #cmd("UINT32 ufs_set_create_perm(UFS *, UINT32)")],
      [Read and set a value of the handle (0755 at the start; the setter returns
      the old value). UFSD never receives it: new files get #cmd("rw-r--r--") and new
      directories #cmd("rwxr-xr-x").],
    [#cmd("void *ufs_get_acee(UFS *)"), #cmd("void *ufs_set_acee(UFS *, void *)")],
      [Read and set a pointer in the handle that nothing uses.],
    [#cmd("UFSSYS *ufs_get_sys"), #cmd("ufs_set_sys"), #cmd("ufs_sys_new"),
      #cmd("ufs_sys_term")], [A system handle for programs that were written
      for ufs370. It holds nothing.],
    [#cmd("int ufs_signon(UFS *, const char *, const char *, const char *)"),
      #cmd("void ufs_signoff(UFS *)")], [Do nothing. #cmd("ufs_signon") always
      returns 0 and does not check the password. Use #cmd("ufs_setuser").],
  )
]

== Limits of the Library <ref-lib-limits>

The limits of UFSD are in @ref-lim-limits. These come from the library or
from the way the program uses it:

#tab(caption: [Limits that come from libufs])[
  #table(columns: (2.4in, 1fr),
    [Item], [Notes],
    [Memory for each open file], [About 8 KB of the heap of the program, for
      the two buffers of 4096 bytes. If the region is too small, #cmd("ufs_fopen")
      returns NULL with #cmd("UFSD_RC_IO"), before the 64 files of the session
      are reached (37 in a batch step with the default region of the test
      system).],
    [Memory for each directory entry], [About 112 bytes of the heap while the
      directory is open.],
    [Name in a directory entry], [59 characters.],
    [Path], [255 characters; 251 for #cmd("ufs_fopen"). The table in
      @ref-lib-dir says what a longer path does.],
    [Files of a session], [64, the open directories of #cmd("ufs_diropen")
      included. All sessions together have 256 (@ref-lim-limits).],
    [Entries in a directory], [1024 at the block size of 4096 (@ref-lim-limits).],
    [Bytes for each request], [4096 (the 4K path). Writing sends a piece of
      248 bytes inline if the common storage has no free buffer.],
  )
]
