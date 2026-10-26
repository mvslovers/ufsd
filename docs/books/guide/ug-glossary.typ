#import "../bookmaster/bookmaster.typ": *

= Glossary <ug-glossary>

This glossary defines the terms of UFSD, and of MVS and SMP as far as this
book uses them. A term printed in _italics_ in a definition has an entry of
its own.

#let g(..items) = deflist(width: 1.45in, ..items.pos().enumerate().map(
  ((i, x)) => if calc.even(i) { par(justify: false, strong(x)) } else { x }))

#g(
  [ACDS], [The SMP data base of the _accepted_ SYSMODs. Its counterpart for
    the applied ones is the _CDS_ (@ug-remove-uclin).],
  [anchor], [The control block in the common storage from which UFSD reaches
    all its other storage. A _reclaim_ finds it through the _SSCT_ (@ug-recover).],
  [APF], [Authorized program facility. A program that runs with authority
    must be loaded from an APF-authorised library, or UFSD gets its authority
    from the SVC of RAKF (@ug-install-auth).],
  [ASCB], [The address space control block. UFSD stores the one of the server
    in the _anchor_, to tell a live server from an orphan (@ug-recover-guard).],
  [BDAM], [The basic direct access method. UFSD reads and writes the
    blocks of a _file system_ with it.],
  [block], [The unit of space of a _file system_. The size is 4096 bytes
    unless the format says otherwise (@ug-disks-space).],
  [CDS], [The SMP data base of the _applied_ SYSMODs. See _ACDS_.],
  [client], [A program in another address space that uses UFSD through the
    subsystem interface, such as HTTPD or FTPD (@ug-intro).],
  [CSA], [The common service area. The storage that all address spaces can
    reach. UFSD keeps its _anchor_, its pools and its _SSI router_ there
    (@ug-intro-how).],
  [file system], [One formatted BDAM data set with a boot block, a
    _superblock_, an inode list and data blocks (@ug-intro-fs).],
  [FMID], [The function modification identifier. The identifier of the
    release in the SMP inventory, #cmd("TUFS152") for UFSD 1.5.2
    (@ug-remove).],
  [inode], [The record of a file or directory in a _file system_. The number
    is fixed when the disk is formatted (@ug-disks-space).],
  [MODIFY], [The console command #cmd("F") that sends a command to a running
    started task (@ug-ops-commands).],
  [mount], [The act of attaching a _file system_ to a directory of the tree,
    and the state afterwards (@ug-config).],
  [mount point], [The directory on which a _file system_ is _mounted_
    (@ug-config-points).],
  [orphan], [The common storage of a server that has ended without freeing it
    (@ug-recover).],
  [reclaim], [The freeing of an _orphan_. The start of UFSD does it, and so
    does #cmd("UFSDCLNP") (@ug-recover-auto).],
  [root], [The first _file system_ of the tree, mounted on #cmd("/").],
  [session], [The connection of one _client_ program to UFSD. It holds the
    open files and the user ID of the client (@ug-ops-stats).],
  [SMP], [The system modification program. It installs UFSD and keeps the
    inventory of what is installed (@ug-install).],
  [SSCT], [The subsystem communication table entry. UFSD registers the
    subsystem #cmd("UFS1") with it.],
  [SSI router], [The module #cmd("UFSDSSIR") in the _CSA_. It takes the
    request of a _client_ from the subsystem interface and queues it for the
    server (@ug-intro-how).],
  [started task], [A program that the operator starts with #cmd("S") and that
    runs without a user. UFSD is one (@ug-ops-start).],
  [subsystem], [An MVS component that is registered by name. UFSD is the
    subsystem #cmd("UFS1").],
  [superblock], [The second block of a _file system_. It describes the
    geometry and the free space, and UFSD writes it back at a stop and at an
    unmount (@ug-ops-stop).],
  [UCLIN], [The SMP statements that edit the inventory directly. They remove
    the _FMID_ of UFSD (@ug-remove-uclin).],
  [UFSD], [The started task and subsystem that serves the file systems.],
  [UFSFMT], [The utility that formats a data set as a _file system_
    (@ug-disks-format).],
  [XMIT], [The format of a data set that was unloaded with TRANSMIT. The
    load modules and the sample library of the release are XMIT files
    (@ug-install-upload).],
)
