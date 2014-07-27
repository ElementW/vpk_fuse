vpk_fuse
=========

A FUSE filesystem which can open Valve PacKage files (VPK).

Supports both VPKv1 and VPKv2, including splitted VPKs (separate archive files)

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
