# Creating UFSD Disks

Each file system that UFSD (ufsd) serves is one data set on MVS. It must be
allocated and formatted before a `ROOT` or `MOUNT` statement names it. This
page only says where to look; the manuals are kept next to the code in
[`docs/books`](books).

| You want to know | Read |
|---|---|
| How to plan the space, allocate the data set and format it with `UFSFMT` (job `UFSD.SAMPLIB(UFSFMT)`), the owner and group, the protection against a reformat, the report | _UFSD User's Guide_ (ML05-0001), chapter 3, "Creating Disks" |
| Every control statement of `UFSFMT`, the phases, the geometry rules, the return codes | _UFSD Reference_ (ML05-0002), chapter 3, "The UFSFMT Utility" |
| The layout of the boot block, the superblock, the inode and the directory entry, the limits of a file system | _UFSD Reference_, chapter 5, "Limits and Disk Format" |
| The meaning of a `UFSFMT` message | _UFSD Reference_, appendix A, "Messages" |

## Disks made on a host

A disk that should arrive with files on it can be built on a host with
[ufsd-utils](https://github.com/mvslovers/ufsd-utils). It writes the same
format as `UFSFMT`, fills the image with files, and uploads it as a data set.
`ufsd-utils help` lists the commands of the build you have.

```
Host                              MVS
ufsd-utils create disk.img
ufsd-utils cp -r ./files disk.img:/
ufsd-utils upload disk.img        --> data set allocated and written
                                      name it in UFSDPRM0, or /F UFSD,MOUNT
```

### Create the image

```
ufsd-utils create root.img --size 1M
```

```
Creating root.img (1M, blksize=4096, inodes=10.0%)
  Volume size:     256 blocks (1.00 MB)
  Block size:      4096 bytes
  Inode blocks:    2 (64 inodes)
  Data blocks:     252 (free: 251)
  Root owner:      (none)/(none)
  Format:          UFS370 v1 (time64 timestamps)

  Upload to MVS:  ufsd-utils upload root.img --dsn YOUR.DATASET.NAME
Done.
```

| Option | Default | Meaning |
|---|---|---|
| `--size` | `10M` | Size of the image. A number with the suffix `K`, `M` or `G` (also `KB`, `MB`, `GB`). |
| `--blksize` | 4096 | Block size, 512 to 8192 in steps of 512. |
| `--inodes` | 10 | Inodes as a percentage of the blocks, 1 to 50. A value outside that range is replaced by 10 without a message. |
| `--owner`, `--group` | none | Owner and group of the root directory. The file system is unowned if you give none. |

The inode list is never shorter than two blocks, as with `UFSFMT`. For the
sizes in the planning table of the _UFSD User's Guide_, chapter 3, "Creating
Disks", `ufsd-utils create` produces the same number of inode blocks.

### Put files on it

```
ufsd-utils mkdir webroot.img:/css
ufsd-utils cp index.html webroot.img:/index.html
ufsd-utils cp -r ./wwwroot/ webroot.img:/
```

An image path is `image:/path`. `webroot.img`, `webroot.img:` and
`webroot.img:/` all name the root directory. Owner and group of a new file are
those of the directory it goes into.

`cp` converts a text file from ASCII to EBCDIC (IBM-1047), and back when it
copies out of the image. It decides by the extension of the file name, ignoring
case: `.html`, `.htm`, `.txt`, `.css`, `.js`, `.xml`, `.json`, `.csv`, `.sh`,
`.c`, `.h`, `.s`, `.md`, `.cfg`, `.conf`, `.toml`, `.yaml`, `.yml`, `.ini`,
`.log`, `.jcl` and `.proc`. **A file with any other extension, or none, is
copied byte for byte**, so a text file of that kind stays ASCII and is not
readable on MVS. Choose explicitly with `-t` (convert) or `-b` (copy as is):

```
ufsd-utils cp -t config.tmpl webroot.img:/config.tmpl
ufsd-utils cp -b logo.png    webroot.img:/logo.png
```

To look at the result:

```
ufsd-utils info  webroot.img            superblock and summary
ufsd-utils ls -l webroot.img:/css       directory listing
ufsd-utils cat   webroot.img:/index.html
ufsd-utils rm    webroot.img:/old.txt   (-r for a directory tree)
ufsd-utils rmdir webroot.img:/empty-dir
```

`cat` converts EBCDIC to ASCII unless you give `-b`.

### Upload the image

```
ufsd-utils upload webroot.img --dsn HTTPD.WEBROOT
```

The command talks to a z/OSMF-compatible REST API on the MVS system, such as
mvsMF, over plain HTTP with the user id and password in HTTP basic
authentication. Give the connection in the environment, or in a `.env` file in
the current directory:

| Variable | Meaning |
|---|---|
| `MVS_HOST` | host name or address |
| `MVS_PORT` | port, default 1080 |
| `MVS_USER`, `MVS_PASS` | user id and password |

`--host` and `--port` override the first two. The command allocates the data
set (`DSORG=PS`, `RECFM=U`, the `BLKSIZE` of the image) and writes the image in
binary. If the data set exists, it stops, unless you give `--replace`: then it
deletes the data set and allocates it again.

You can also allocate the data set yourself, as in chapter 3 of the User's
Guide, and send the image to it with any tool that transfers in binary. The
number of blocks of the image must fit into the extent, and `BLKSIZE` must be
the block size of the image.

When the data set exists, name it in the configuration (_UFSD User's Guide_,
chapter 4, "Configuring UFSD").

The books are attached to each release from 1.6.0 on as `ml05-0001-<edition>.pdf`
and `ml05-0002-<edition>.pdf`. The sources are in [`docs/books`](books); the
chapters are in `guide/` and `ref/`.
