#import "bookmaster/bookmaster.typ": *

#show: book.with(
  title: "UFSD for MVS 3.8j",
  subtitle: "Reference",
  short-title: "UFSD Reference",
  product: "UFSD",
  number: "ML05-0002-1",
  date: "October 2026",
  authors: ("Mike Großmann",),
  edition: [
    #text(font: head-font, weight: "bold", size: 11pt)[First Edition (October 2026)]

    This edition applies to Version 1 Release 5 of UFSD, the virtual
    filesystem server for MVS 3.8j (ufsd 1.5.2), and to all subsequent
    releases and modifications until otherwise indicated in new editions.

    Comments on this book may be addressed to the issue tracker of the
    mvslovers/ufsd repository on GitHub.

    © Copyright Mike Großmann 2026. All rights reserved.
  ],
)

#part("index.html", title: [UFSD Reference])[
#titlepage()
#contents()
#figures()

#heading(numbering: none)[About This Book] <about>

This book describes UFSD (ufsd), the virtual filesystem server for MVS 3.8j,
in full: the operator commands, the statements of the configuration member
UFSDPRM, the statements of the format utility UFSFMT, the started task and
its utilities, the limits, the disk format as far as an operator needs it,
and the messages. How to use them together is the subject of the companion
volume, the _UFSD User's Guide_.

== How This Book Is Organized

#deflist(width: 1.35in,
  [Chapter 1], [“Operator Commands”.],
  [Chapter 2], [“The UFSDPRM Statements”.],
  [Chapter 3], [“The UFSFMT Utility”.],
  [Chapter 4], [“The Started Task and Its Utilities”.],
  [Chapter 5], [“The Client Library libufs”.],
  [Chapter 6], [“Limits and Disk Format”.],
  [Appendix A], [“Messages”.],
)

== Related Publications

#deflist(width: 1.35in,
  [ML05-0001], [_UFSD User's Guide_: installing, configuring, running and
    recovering UFSD, and writing programs that call it through libufs.],
)

#mainmatter()
]
#set page(numbering: "1")

#part("ref-cmd.html", include "ref/ref-cmd.typ")
#part("ref-cfg.html", include "ref/ref-cfg.typ")
#part("ref-fmt.html", include "ref/ref-fmt.typ")
#part("ref-stc.html", include "ref/ref-stc.typ")
#part("ref-lib.html", include "ref/ref-lib.typ")
#part("ref-lim.html", include "ref/ref-lim.typ")

#show: appendices
#part("apx-messages.html", include "ref/apx-messages.typ")

#part("index-terms.html", title: [Index])[
#heading(numbering: none)[Index]
#make-index()
]
