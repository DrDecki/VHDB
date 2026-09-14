# VHDB

A homebrew store for the PS Vita, built on the
[VitaHomebrewDB](https://github.com/DrDecki/VitaHomebrewDB) catalog.

## Using it

Install `vhdb.vpk` from the
[latest release](https://github.com/DrDecki/VHDB/releases/tag/catalog). On first
start it fetches the catalog and the icon pack by itself, so give it a moment on a
Wi-Fi connection. After that it starts instantly.

| Button | In the list | On the detail page |
| --- | --- | --- |
| Cross | open the entry | install it |
| Square | search | fetch the data files |
| Circle | clear the search | back |
| Triangle | sort by name or date | |
| L R | switch category, hold L and tap R for something random | |
| Select | check for a newer catalog | |
| Start | read ux0:app and work out what is installed | |

Installing a port pulls its data files along and unpacks them into `ux0:data`.
Before unpacking it shows what the archive actually contains, because a handful of
them hold loose files that do not belong there.

Plugins and PC tools are in the catalog but not offered here. A plugin needs a line
in `config.txt` and a reboot, and a store that does that on its own can leave a
console that does not boot. Download those on a computer.

## Building

Needs [VitaSDK](https://vitasdk.org):

    vdpm vita2d curl openssl zstd

    export VITASDK=/usr/local/vitasdk
    export PATH=$VITASDK/bin:$PATH
    cd vita && cmake -B build . && cmake --build build

The code in `core/` is plain C99 without platform calls and builds on a PC as well.
`stubs/` carries enough of the Vita headers to syntax check the client without the
SDK.

## How the catalog gets here

`apps.json` in VitaHomebrewDB is the source of truth. Parsing 1400 JSON entries on
a handheld is slow, so `tools/make_vhdb.py` turns it into `vhdb.bin`: fixed 160
byte records, one string blob, two sort orders, a CRC32 over the lot. The client
loads it in one read and uses the strings in place.

A workflow rebuilds it every night and publishes only when the checksum changed.
The client asks for the first 64 bytes over a range request, compares that same
checksum, and downloads the rest only when there is a point.

`tools/hash_eboots.py` reads the eboot out of each VPK using range requests against
the zip index, so it pulls a couple of megabytes instead of the whole archive.
`tools/refresh_rolling.py` handles entries whose URL points at a `continuous` or
`nightly` target, where the file behind the link changes without anyone updating
the catalog.

## Why it does not read versions

Version strings in `param.sfo` are unreliable. Plenty of homebrew ships with
`00.00` and never touches it, so comparing them invents updates that do not exist.

VHDB compares files instead. The catalog stores the MD5 of `eboot.bin` as it sits
inside the VPK, and the client hashes the copy on the card. For Unity, GameMaker,
Godot and Lua ports the eboot is only a loader and stays the same between versions,
so the engine's data file is hashed as well. This works for everything on the
console, not only for what was installed through VHDB.

## Credits

The fake package header used to install VPKs, `vita/head_bin.h` and the HMAC in
`vita/install.c`, comes from VHBB by devnoname120, by way of VitaShell and VitaDB
Downloader. VHDB is GPL3 for that reason.

The catalog itself is not covered by the license. Names, descriptions, changelogs
and icons belong to the people who made the homebrew.
