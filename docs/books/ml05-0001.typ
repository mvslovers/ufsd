#import "bookmaster/bookmaster.typ": *

#show: book.with(
  title: "UFSD for MVS 3.8j",
  subtitle: "User's Guide",
  short-title: "UFSD User's Guide",
  product: "UFSD",
  number: "ML05-0001-0",
  date: "October 2026",
  authors: ("Mike Großmann",),
  edition: [
    #text(font: head-font, weight: "bold", size: 11pt)[First Edition (October 2026)]

    This edition applies to Version 1 Release 5 of UFSD, the virtual
    filesystem server for MVS 3.8j (ufsd 1.5.0), and to all subsequent
    releases and modifications until otherwise indicated in new editions.

    *Draft.* This book describes UFSD 1.5.0 as released. It is written after
    the release and has not yet been read by a second person against the
    code.

    Comments on this book may be addressed to the issue tracker of the
    mvslovers/ufsd repository on GitHub.

    © Copyright Mike Großmann 2026. All rights reserved.
  ],
)

#part("index.html", title: [UFSD User's Guide])[
#titlepage()
#contents()
#figures()

#heading(numbering: none)[About This Book] <about>

This book shows how to install, configure and run UFSD (ufsd), the virtual
filesystem server for MVS 3.8j: how to put it on a system, how to create the
disks it serves, how to describe them in the configuration member, how an
operator starts, queries and stops it, what to do after an abend, and how to
remove it. A chapter shows how to write a C or assembler program that uses
UFSD through its client library. Every operator command, configuration parameter, utility statement
and message in full is the subject of the companion volume, the _UFSD
Reference_.

The book describes UFSD 1.5.0, as released. The functions of the client
library `libufs` are described in full in the _UFSD Reference_.

== How This Book Is Organized

#deflist(width: 1.35in,
  [Chapter 1], [“Introducing UFSD”.],
  [Chapter 2], [“Installing UFSD”.],
  [Chapter 3], [“Creating Disks”.],
  [Chapter 4], [“Configuring UFSD”.],
  [Chapter 5], [“Running UFSD”.],
  [Chapter 6], [“Programming with libufs”.],
  [Chapter 7], [“Recovery After a Failure”.],
  [Chapter 8], [“Removing UFSD”.],
  [Appendix A], [“Glossary”.],
)

== Related Publications

#deflist(width: 1.35in,
  [ML05-0002], [_UFSD Reference_: the operator commands, the UFSDPRM
    statements, the UFSFMT statements, the started task, the client library
    libufs, the limits and the messages of UFSD.],
)

#mainmatter()
]
#set page(numbering: "1")

#part("ug-intro.html", include "guide/ug-intro.typ")
#part("ug-install.html", include "guide/ug-install.typ")
#part("ug-disks.html", include "guide/ug-disks.typ")
#part("ug-config.html", include "guide/ug-config.typ")
#part("ug-ops.html", include "guide/ug-ops.typ")
#part("ug-libufs.html", include "guide/ug-libufs.typ")
#part("ug-recover.html", include "guide/ug-recover.typ")
#part("ug-remove.html", include "guide/ug-remove.typ")

#show: appendices
#part("ug-glossary.html", include "guide/ug-glossary.typ")

#part("index-terms.html", title: [Index])[
#heading(numbering: none)[Index]
#make-index()
]
