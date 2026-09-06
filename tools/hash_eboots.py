import hashlib
import json
import os
import struct
import subprocess
import sys
import zlib

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
CATALOG = os.environ.get("VHDB_CATALOG") or os.path.expanduser("~/vitadbtoo/VitaHomebrewDB")
CACHE = os.path.join(REPO, "eboot_hashes.json")

AUX_FILES = [
    ("Media/sharedassets0.assets.resS", 1),
    ("games/game.win", 2),
    ("index.lua", 3),
    ("main.lua", 4),
    ("game.apk", 5),
    ("game_data/game.pck", 6),
]

SOURCES = ["apps.json", "psp_apps.json"]


def fetch(url, start=None, end=None):
    command = ["curl", "-sL", "--max-time", "120", "--fail"]
    if start is not None:
        command += ["-r", "%d-%s" % (start, "" if end is None else str(end))]
    command += [url]
    result = subprocess.run(command, capture_output=True)
    if result.returncode != 0:
        return None
    return result.stdout


def total_size(url):
    result = subprocess.run(
        ["curl", "-sIL", "--max-time", "60", url], capture_output=True, text=True
    )
    if result.returncode != 0:
        return 0
    length = 0
    for line in result.stdout.splitlines():
        if line.lower().startswith("content-length:"):
            length = int(line.split(":", 1)[1].strip())
    return length


def read_directory(url, size):
    tail_length = min(size, 66000)
    tail = fetch(url, size - tail_length)
    if not tail:
        return None
    marker = tail.rfind(b"PK\x05\x06")
    if marker < 0:
        return None

    count = struct.unpack("<H", tail[marker + 10:marker + 12])[0]
    directory_size = struct.unpack("<I", tail[marker + 12:marker + 16])[0]
    directory_offset = struct.unpack("<I", tail[marker + 16:marker + 20])[0]

    if directory_offset == 0xFFFFFFFF or count == 0xFFFF:
        locator = tail.rfind(b"PK\x06\x07")
        if locator < 0:
            return None
        end64_offset = struct.unpack("<Q", tail[locator + 8:locator + 16])[0]
        end64 = fetch(url, end64_offset, end64_offset + 55)
        if not end64 or end64[:4] != b"PK\x06\x06":
            return None
        count = struct.unpack("<Q", end64[32:40])[0]
        directory_size = struct.unpack("<Q", end64[40:48])[0]
        directory_offset = struct.unpack("<Q", end64[48:56])[0]

    start = size - tail_length
    if directory_offset >= start:
        directory = tail[directory_offset - start:directory_offset - start + directory_size]
    else:
        directory = fetch(url, directory_offset, directory_offset + directory_size - 1)
    if not directory:
        return None

    entries = {}
    position = 0
    for _ in range(count):
        if directory[position:position + 4] != b"PK\x01\x02":
            break
        method = struct.unpack("<H", directory[position + 10:position + 12])[0]
        compressed = struct.unpack("<I", directory[position + 20:position + 24])[0]
        name_length = struct.unpack("<H", directory[position + 28:position + 30])[0]
        extra_length = struct.unpack("<H", directory[position + 30:position + 32])[0]
        comment_length = struct.unpack("<H", directory[position + 32:position + 34])[0]
        local_offset = struct.unpack("<I", directory[position + 42:position + 46])[0]
        name = directory[position + 46:position + 46 + name_length].decode("utf-8", "ignore")
        extra = directory[position + 46 + name_length:position + 46 + name_length + extra_length]

        if compressed == 0xFFFFFFFF or local_offset == 0xFFFFFFFF:
            cursor = 0
            while cursor + 4 <= len(extra):
                tag, field_size = struct.unpack("<HH", extra[cursor:cursor + 4])
                body = extra[cursor + 4:cursor + 4 + field_size]
                if tag == 1:
                    values = list(struct.unpack("<%dQ" % (len(body) // 8), body[:len(body) // 8 * 8]))
                    index = 0
                    if struct.unpack("<I", directory[position + 24:position + 28])[0] == 0xFFFFFFFF:
                        index += 1
                    if compressed == 0xFFFFFFFF and index < len(values):
                        compressed = values[index]
                        index += 1
                    if local_offset == 0xFFFFFFFF and index < len(values):
                        local_offset = values[index]
                cursor += 4 + field_size

        entries[name.replace("\\", "/")] = (local_offset, compressed, method)
        position += 46 + name_length + extra_length + comment_length

    return entries


def member_md5(url, entry):
    local_offset, compressed, method = entry
    header = fetch(url, local_offset, local_offset + 29)
    if not header or header[:4] != b"PK\x03\x04":
        return None
    name_length = struct.unpack("<H", header[26:28])[0]
    extra_length = struct.unpack("<H", header[28:30])[0]
    start = local_offset + 30 + name_length + extra_length
    payload = fetch(url, start, start + compressed - 1)
    if payload is None or len(payload) != compressed:
        return None
    if method == 8:
        payload = zlib.decompressobj(-15).decompress(payload)
    elif method != 0:
        return None
    return hashlib.md5(payload).hexdigest()


def load_catalogs():
    apps = []
    for name in SOURCES:
        path = os.path.join(CATALOG, name)
        if not os.path.exists(path):
            continue
        with open(path, "r", encoding="utf-8") as handle:
            data = json.load(handle)
        if isinstance(data, dict):
            data = list(data.values())
        apps.extend(data)
    return apps


def main():
    limit = int(sys.argv[1]) if len(sys.argv) > 1 else 0

    cache = {}
    if os.path.exists(CACHE):
        with open(CACHE, "r", encoding="utf-8") as handle:
            cache = json.load(handle)

    apps = load_catalogs()
    done = 0
    skipped = 0
    failed = 0

    for app in apps:
        app_id = str(app.get("id") or "")
        url = (app.get("url") or "").strip()
        vpk_hash = (app.get("hash") or "").strip().lower()
        if not app_id or not url:
            continue

        known = cache.get(app_id)
        if known and known.get("vpk") == vpk_hash and vpk_hash:
            skipped += 1
            continue
        if limit and done >= limit:
            break

        size = total_size(url)
        if size <= 0:
            failed += 1
            print("no size    %-6s %s" % (app_id, app.get("name")))
            continue

        entries = read_directory(url, size)
        if not entries:
            failed += 1
            print("no zip     %-6s %s" % (app_id, app.get("name")))
            continue

        record = {"vpk": vpk_hash, "eboot": "", "aux": "", "aux_kind": 0}

        target = None
        for name in sorted(entries, key=len):
            lowered = name.lower()
            if lowered.endswith("eboot.bin") or lowered.endswith("eboot.pbp"):
                target = name
                break
        if target:
            digest = member_md5(url, entries[target])
            if digest:
                record["eboot"] = digest

        for suffix, kind in AUX_FILES:
            match = None
            for name in sorted(entries, key=len):
                if name.lower().endswith(suffix.lower()):
                    match = name
                    break
            if not match:
                continue
            digest = member_md5(url, entries[match])
            if digest:
                record["aux"] = digest
                record["aux_kind"] = kind
            break

        if not record["eboot"] and not record["aux"]:
            failed += 1
            print("no eboot   %-6s %s" % (app_id, app.get("name")))
            continue

        cache[app_id] = record
        done += 1
        print("ok         %-6s %-38s %s %s" % (app_id, str(app.get("name"))[:38],
                                               record["eboot"][:8],
                                               "aux%d" % record["aux_kind"] if record["aux_kind"] else ""))

        if done % 20 == 0:
            with open(CACHE, "w", encoding="utf-8") as handle:
                json.dump(cache, handle, indent=1, sort_keys=True)

    with open(CACHE, "w", encoding="utf-8") as handle:
        json.dump(cache, handle, indent=1, sort_keys=True)

    print("")
    print("hashed %d, already known %d, failed %d, cache holds %d" %
          (done, skipped, failed, len(cache)))


if __name__ == "__main__":
    main()
