#import "../bookmaster/bookmaster.typ": *

= The UFSFMT Utility <ref-fmt>

#idx("UFSFMT")
#idx("format")
UFSFMT is the batch program that formats a BDAM data set as a UFSD file system
with an empty root directory. It is the load module #cmd("UFSFMT") of
#cmd("UFSD.LINKLIB"). It is not authorised and runs as a normal job step. How
to plan and allocate a disk is the subject of the _UFSD User's Guide_. This
chapter describes the program itself.

== Job Control <ref-fmt-jcl>

#tab(caption: [The DD statements of UFSFMT])[
  #table(columns: (1.1in, 1.2in, 1fr),
    [DD name], [Needed], [Use],
    [#cmd("STEPLIB")], [yes], [The library with the module, #cmd("UFSD.LINKLIB").
      The module is not authorised, so the library need not be in the APF list.],
    [#cmd("SYSIN")], [no], [The control statements (@ref-fmt-sysin). May be
      omitted or be #cmd("DUMMY"). Then every default applies.],
    [#cmd("DISKFILE")], [yes], [The data set to format. The name can be changed
      with the statement #cmd("DDNAME"). It must be allocated #cmd("DISP=OLD").],
    [#cmd("SYSPRINT")], [yes], [The progress messages and the report, in mixed case
      (@ref-fmt-streams).],
    [#cmd("SYSTERM")], [yes], [Warnings and errors, in upper case.],
  )
]

#screen(```
//FORMAT   EXEC PGM=UFSFMT
//STEPLIB  DD  DISP=SHR,DSN=UFSD.LINKLIB
//DISKFILE DD  DISP=OLD,DSN=IBMUSER.UFSHOME
//SYSPRINT DD  SYSOUT=*
//SYSTERM  DD  SYSOUT=*
//SYSIN    DD  *
   BLKSIZE  4096
   INODES   10.0
   OWNER    IBMUSER
   GROUP    USER
/*
```)

#cmd("PARM=") is not supported. A parameter on the #cmd("EXEC") statement gets
#cmd("UFSFMT09W") on #cmd("SYSTERM") and is otherwise ignored. The control
statements are read from #cmd("SYSIN") and from nowhere else.

Only the primary extent of the data set is formatted. A secondary quantity
would never be used, and the space it brings would be invisible to the file
system. MVS rounds an allocation up to a track boundary, so the disk may be a
little larger than requested. UFSFMT formats what the primary extent
provides and reports the true block count in #cmd("UFSFMT26I").

#cmd("DISP=OLD") is required. A data set that is allocated #cmd("DISP=SHR") is
refused with #cmd("UFSFMT25E"), because the format would wipe a disk that
another job may be reading.

== The Control Statements <ref-fmt-sysin>

#idx("SYSIN")
#idx("control statements")
UFSFMT reads #cmd("SYSIN") as a stream of tokens that are separated by blanks or
tab characters. A keyword and its value may be on one line or on two. Several
keywords may share a line. Four rules govern the text:

- Each record is read as a card image. Columns 73 to 80 are the sequence
  field and are never part of a statement.
- A comment starts with #cmd("/*") and ends with #cmd("*/"). It may span
  records, and it separates two tokens. In an instream #cmd("SYSIN") the
  text #cmd("/*") must not stand in column 1, because JES takes that for the
  end of the data.
- Keywords are written in full. An abbreviation is not accepted. Lower case
  is accepted and folded to upper case, as are the values of #cmd("DDNAME"),
  #cmd("OWNER") and #cmd("GROUP").
- A keyword may appear once. A second occurrence is an error and not an
  override (#cmd("UFSFMT12E")). The value that follows the repeated keyword is
  skipped, so that it is not reported as a second error.

#tab(caption: [The keywords of UFSFMT])[
  #table(columns: (0.9in, 1.7in, 0.8in, 1fr),
    [Keyword], [Value], [Default], [Meaning],
    [#cmd("BLKSIZE")], [Digits only. 512 to 8192, a multiple of 512], [4096],
      [The block size in bytes. It should equal the #cmd("BLKSIZE") of the
      allocation.],
    [#cmd("DDNAME")], [1 to 8 characters (A to Z, 0 to 9, @, #sym.hash, \$), not
      starting with a digit], [#cmd("DISKFILE")],
      [The DD statement of the data set to format.],
    [#cmd("INODES")], [Digits with at most one decimal point. 1.0 to 50.0], [10.0],
      [The percentage of inode blocks (@ref-fmt-geometry).],
    [#cmd("OWNER")], [The same rule as DDNAME], [none],
      [The owner stored in the root directory.],
    [#cmd("GROUP")], [The same rule as DDNAME], [none],
      [The group stored in the root directory.],
    [#cmd("FORCE")], [], [off], [Format over an existing file system.],
    [#cmd("QUIET")], [], [off],
      [Suppress the messages and the report of #cmd("SYSPRINT").],
    [#cmd("VERBOSE")], [], [off], [Print the effective parameters and one
      message for each phase.],
    [#cmd("HELP")], [], [], [Print the help text and stop, return code 4.],
  )
]

The statements #cmd("OWNER") and #cmd("GROUP") are metadata of the root
directory. They do not decide who may write to the file system. That is
decided by the #cmd("OWNER()") parameter of the #cmd("MOUNT") statement of
the configuration (@ref-cfg-access). Without them the root directory is
unowned, and the report shows #cmd("(none)").

A value that is not valid gets the error that belongs to its keyword
(#cmd("UFSFMT01E") to #cmd("UFSFMT04E")). UFSFMT goes on reading, reports every
error it finds, ends the scan with #cmd("UFSFMT15E PARAMETER ERRORS -- DATASET NOT
FORMATTED") and returns 8. An unknown word is #cmd("UFSFMT11E"). A value
keyword with nothing after it at the end of #cmd("SYSIN") is
#cmd("UFSFMT13E"), and a read error on #cmd("SYSIN") is #cmd("UFSFMT17E"). A
missing #cmd("SYSIN") DD (not a #cmd("DUMMY") one) gives #cmd("UFSFMT14W") and
the defaults.

=== HELP <ref-fmt-help>

#idx("HELP", "UFSFMT")
When #cmd("HELP") is given, UFSFMT prints a table of the keywords and the DD
statements, an explanation of the extent and an example job, and ends with
return code 4. It writes nothing to the data set. The text goes to
#cmd("SYSPRINT") and is not suppressed by #cmd("QUIET"). #cmd("HELP") wins over
parameter errors: the errors are shown, but the return code is 4 and the help
text is printed.

== What UFSFMT Does <ref-fmt-phases>

#idx("phases", "format")
The program runs in phases. The order matters for what is left on the disk
when a phase fails.

#tab(caption: [The phases of UFSFMT])[
  #table(columns: (0.9in, 1fr),
    [Phase], [Work],
    [Parse], [Reads #cmd("SYSIN"). Prints the banner #cmd("UFSFMT10I"). The banner
      appears also when #cmd("HELP") or a parameter error ends the run.
      #cmd("QUIET") suppresses it.],
    [Probe], [Reads block 0. If the data set holds a UFSD file system, the
      program stops with #cmd("UFSFMT05E") unless #cmd("FORCE") is given, in
      which case it prints #cmd("UFSFMT05W") and goes on (@ref-fmt-protect).],
    [Fill], [Opens the data set for output and writes zero blocks into the primary
      extent until the extent is full. The number of blocks written is the
      size of the file system (#cmd("UFSFMT26I")).],
    [Geometry], [Computes the layout from the block count, the block size and
      #cmd("INODES") (@ref-fmt-geometry).],
    [Format], [Writes the inode list and the chain of free blocks.],
    [Root], [Writes the root inode and the root directory block.],
    [Super], [Writes the superblock.],
    [Boot], [Writes block 0, the boot block, last.],
    [Report], [Prints the summary and the #cmd("MOUNT") statement.],
  )
]

The boot block is written last on purpose. It is the only block that marks the
data set as a UFSD file system, so a run that is interrupted before it leaves
a data set that UFSD refuses to mount (#cmd("UFSD044E")) and that UFSFMT treats
as unformatted.

=== The Fill Phase <ref-fmt-fill>

The size of the file system is not read from the catalog or the volume. UFSFMT
writes blocks until MVS reports the end of the extent. The completion codes
B37, D37 and E37 are taken as that end. Any other abend of a write is
#cmd("UFSFMT28E"), which gives the number of blocks written before it. If the
end of the extent is not reached after 1048575 blocks, the program stops
with #cmd("UFSFMT24E"), which names 1048576 as the count. The count is a
safeguard against a loop that never ends, not a limit of the file system. In
practice it means that an extent of 1048576 blocks or more (4 GB at the block
size 4096) is refused and not cut. If not even the first block can be written, the message is #cmd("UFSFMT24E NO
BLOCKS COULD BE WRITTEN").

A data set that is too small is refused with #cmd("UFSFMT27E"). That is a
data set of fewer than 8 blocks, or one in which the boot block, the
superblock, the inode list of at least two blocks and one data block for the
root directory do not fit. The check follows the fill phase, so the extent
has already been zeroed when the message appears.

=== Reformat Protection <ref-fmt-protect>

#idx("FORCE")
The probe reads block 0 of the data set and not the catalog. Three cases
exist:

#tab(caption: [What the probe finds])[
  #table(columns: (2in, 1fr),
    [Finding], [Result],
    [A UFSD file system, and no #cmd("FORCE")], [#cmd("UFSFMT05E"), then
      #cmd("UFSFMT06I") (size) and #cmd("UFSFMT07I") (creation time) when
      they are known, then #cmd("UFSFMT08I SPECIFY FORCE TO OVERWRITE"). Return
      code 8. Nothing was written.],
    [A UFSD file system, and #cmd("FORCE")], [#cmd("UFSFMT05W"), #cmd("UFSFMT06I")
      and #cmd("UFSFMT07I"), and the format goes on.],
    [No file system, or block 0 cannot be read], [The format goes on. A data
      set that was never written cannot be read, and there is nothing to
      protect.],
    [The block size of the data set is outside 512 to 8192], [The probe is
      skipped and the format goes on, also over an existing file system, and
      also without #cmd("FORCE").],
  )
]

Because the probe reads the tracks and not the catalog, a new data set that
reuses the tracks of a deleted file system is found as a file system too.
Specify #cmd("FORCE") in that case. The messages #cmd("UFSFMT06I") and
#cmd("UFSFMT07I") are in mixed case.

== Geometry <ref-fmt-geometry>

#idx("geometry")
#idx("inode")
The layout follows from three numbers: the block count _n_ that the fill
phase found, the block size _b_, and the percentage _p_ of #cmd("INODES").

#tab(caption: [How UFSFMT computes the layout])[
  #table(columns: (1.8in, 1fr),
    [Value], [Rule],
    [Inodes per block], [_b_ / 128],
    [Inode blocks], [_n_ / (_b_ / 128) × _p_ / 100, rounded to the nearest
      integer, but at least 2],
    [Inode slots], [inode blocks × (_b_ / 128)],
    [First data block], [2 + inode blocks],
    [Data blocks], [_n_ − first data block],
    [Free inodes], [inode slots − 2 (inode 1 is reserved and inode 2 is the
      root)],
    [Free blocks], [data blocks − 1 (the root directory takes one)],
  )
]

#cmd("INODES") is therefore not a percentage of the disk. It is the percentage
of the blocks that would be needed to give every block an inode of its own.
The inode list is never shorter than two blocks.

The root directory block is taken from the free block cache of the superblock
as the last of its entries: it is the 51st data block on a disk with 51 or
more data blocks, and the last data block on a smaller one. The free blocks
that do not fit in the 51 entries of the superblock are linked in chain
blocks (@ref-lim-format).

The check of the layout can fail in two ways. #cmd("UFSFMT27E") reports a
data set that is too small to hold the inode list and one data block. Any other
failure of the arithmetic is #cmd("UFSFMT48E"), which cannot be caused by a
statement that was accepted.

== Messages and Streams <ref-fmt-streams>

#idx("SYSTERM")
#idx("SYSPRINT")
The messages of UFSFMT go to one of two files, and the case of the text tells
which:

#tab(caption: [Where the messages of UFSFMT go])[
  #table(columns: (0.9in, 1.1in, 1fr),
    [File], [Case], [Messages],
    [#cmd("SYSPRINT")], [Mixed], [The banner #cmd("UFSFMT10I"); #cmd("UFSFMT26I"),
      #cmd("UFSFMT51I"), #cmd("UFSFMT52I"), #cmd("UFSFMT71I"); the report
      #cmd("UFSFMT80I") to #cmd("UFSFMT86I"); #cmd("UFSFMT90I") and
      #cmd("UFSFMT91I"). #cmd("QUIET") suppresses all of them. The messages
      of #cmd("VERBOSE") (#cmd("UFSFMT16I"), #cmd("UFSFMT19I"),
      #cmd("UFSFMT20I"), #cmd("UFSFMT39I"), #cmd("UFSFMT40I"),
      #cmd("UFSFMT59I"), #cmd("UFSFMT60I"), #cmd("UFSFMT65I"),
      #cmd("UFSFMT79I"), #cmd("UFSFMT99I")) are in upper case; #cmd("QUIET")
      suppresses them, too.],
    [#cmd("SYSTERM")], [Upper], [All warnings and errors. #cmd("QUIET") never
      suppresses them, because a format that failed must say so. The
      informational messages #cmd("UFSFMT06I"), #cmd("UFSFMT07I") and
      #cmd("UFSFMT08I") also go here, and #cmd("UFSFMT06I") and
      #cmd("UFSFMT07I") are in mixed case.],
  )
]

The complete list with the exact texts is in @apx-messages-fmt.

== The Report <ref-fmt-report>

#idx("report")
At the end of a successful run the program prints the report and, last, the
statement that mounts the new disk. The following run formatted a data set
of 256 blocks. It was recorded with release 1.5.0. Releases 1.5.1 and 1.5.2 print the
same report with their own version in the first line, because UFSFMT did not
change between the three releases.

#screen(```
UFSFMT10I UFSFMT 1.5.0 -- UFS370 disk format utility
UFSFMT26I Initialized 256 blocks (1.00 MB)
UFSFMT51I Formatted 2 index blocks, 64 inode slots
UFSFMT52I Formatted 252 data blocks
UFSFMT71I Root directory created, owner=HERC01, group=(none), mode=0755

UFSFMT80I Format summary
UFSFMT81I   Dataset . . . . . HERC01.UFSHOME
UFSFMT82I   Block size  . . . 4096
UFSFMT83I   Total blocks  . . 256           (1.00 MB)
UFSFMT84I   Inode blocks  . . 2             (64 slots, 62 free)
UFSFMT85I   Data blocks . . . 252           (251 free)
UFSFMT86I   Root owner  . . . HERC01/(none)

UFSFMT90I Add to your UFSD parmlib member:
UFSFMT91I   MOUNT    DSN(HERC01.UFSHOME) PATH(/your/mount/point) MODE(RW) OWNER(HERC01)
```)

The root directory gets the mode 0755 (#cmd("drwxr-xr-x")). The block size of the
example is 4096, so a block holds 32 inodes, and #cmd("INODES 10.0") of 256
blocks rounds to one inode block, which the minimum raises to two.

The line #cmd("UFSFMT91I") is a model for the configuration member. The path is
a placeholder that you replace. The line carries #cmd("OWNER()") only when the
disk was formatted with #cmd("OWNER"), because an empty #cmd("OWNER()") would
be a syntax error to the parser of the configuration, and a mount without an
owner means that any user can write to it (@ref-cfg-access). If the data
set name is not known, the line shows #cmd("your.dataset.name").

== Return Codes <ref-fmt-rc>

#idx("return codes", "UFSFMT")
#tab(caption: [The return codes of UFSFMT])[
  #table(columns: (0.6in, 1fr),
    [Code], [Meaning],
    [0], [The data set was formatted. The report was printed, unless
      #cmd("QUIET") was given.],
    [4], [#cmd("HELP") was given. Nothing was written.],
    [8], [The run failed: a parameter error (#cmd("UFSFMT15E")), an existing
      file system without #cmd("FORCE") (#cmd("UFSFMT05E")), a DD that cannot be
      allocated or opened, a data set that is too small, or a failed write.
      Read #cmd("SYSTERM") for the message.],
  )
]

A return code of 8 after the fill phase may leave the data set zeroed and
without a boot block. Such a data set is not a file system, and UFSD does not
mount it. Format it again.
