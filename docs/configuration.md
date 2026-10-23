# Configuring UFSD

The configuration of UFSD (ufsd) is described in the two manuals that are
kept next to the code in [`docs/books`](books). This page only says where to
look.

| You want to know | Read |
|---|---|
| How the started task procedure and the `UFSDPRM0` member fit together, what `ROOT` and `MOUNT` do, who may write to a mount, what happens when the member cannot be used | _UFSD User's Guide_ (ML05-0001), chapter 4, "Configuring UFSD" |
| Every statement and keyword of the member, the rules of the parser, a complete member, the errors | _UFSD Reference_ (ML05-0002), chapter 2, "The UFSDPRM Statements" |
| The operator commands (`STATS`, `SESSIONS`, `MOUNT`, `UNMOUNT`, `TRACE`, `REBUILD`, `HELP`, `SHUTDOWN`) | _UFSD User's Guide_, chapter 5, "Running UFSD", and _UFSD Reference_, chapter 1, "Operator Commands" |
| The procedure `UFSD`, its symbolic parameters, the load modules | _UFSD Reference_, chapter 4, "The Started Task and Its Utilities" |
| Authorization (RAKF or the APF list) | _UFSD User's Guide_, chapter 2, "Installing UFSD", section "Authority" |
| The meaning of a console message | _UFSD Reference_, appendix A, "Messages" |

The books are attached to each release from 1.5.0 on as `ml05-0001-<edition>.pdf`
and `ml05-0002-<edition>.pdf`. The sources are in [`docs/books`](books); the
chapters are in `guide/` and `ref/`.
