# Recovery and Troubleshooting

This page only says where to look for what to do when UFSD (ufsd) ends in a
way it did not plan. The manuals are kept next to the code in
[`docs/books`](books).

| You want to know | Read |
|---|---|
| What an abend, a `C UFSD` or a hang leaves behind, why the next `S UFSD` frees it, when a start refuses, what `UFSDCLNP` does, how to take a server down that does not react | _UFSD User's Guide_ (ML05-0001), chapter 6, "Recovery After a Failure" |
| How the start and the stop work, the abend path, the reclaim, the return codes, `UFSDCLNP` | _UFSD Reference_ (ML05-0002), chapter 4, "The Started Task and Its Utilities" |
| What to do about a problem during installation or at the first start | _UFSD User's Guide_, chapter 2, "Installing UFSD", section "When Something Goes Wrong" |
| The meaning of a console message, with the action to take | _UFSD Reference_, appendix A, "Messages" |
| What UFSD does not do (access control per mount only, no locking, the limit on the size of a file) | _UFSD User's Guide_, chapter 1, "Introducing UFSD", section "What UFSD Does Not Do" |
| The subsystem name `UFS1` and what to do on a system that ever ran UFSD 1.0.0 | _UFSD User's Guide_, chapter 6, section "Changing the Subsystem Name" |

The books are attached to each release from 1.5.1 on as `ml05-0001-<edition>.pdf`
and `ml05-0002-<edition>.pdf`. The sources are in [`docs/books`](books); the
chapters are in `guide/` and `ref/`.
