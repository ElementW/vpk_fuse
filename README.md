vpk_fuse
=========

A FUSE filesystem which can open Valve PacKage files (VPK).

Supports both VPKv1 and VPKv2, including splitted VPKs (separate archive files)

Compiling
----

What you need:
 - `make`
 - A `gcc` version that supports GNU C99
 - `pkg-config`
 - FUSE headers, `libfuse-dev` package on Debian-based systems, or simply `fuse` on Arch Linux. For other OSes, just make sure the headers exist and are correctly pointed to by `pkg-config`

How to build (caution: extremely hard):
 1. `git clone --depth 1 git@github.com:ElementW/vpk_fuse.git && cd vpk_fuse`
 2. `make`
 3. Done.

Usage
----

```sh
./vpk_fuse <filename> [FUSE flags] <FUSE mountpoint>
# E.g.:
mkdir /mnt/p2vpk
./vpk_fuse "/mnt/SteamApps/common/portal 2/portal2/pak01_dir.vpk" /mnt/p2vpk
```

Bottom note
----

Coded in two days for fun (and not profit :P ), may be somehow crashy (current error handling isn't great)!

License
----

GPLv3, see "LICENSE" file for details 
