import hashlib
import io
import json
import os
import subprocess
import sys
import zipfile
from datetime import date

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)

SELF_ID = 900000
SELF_TITLEID = "VHDB00001"
VPK_URL = "https://github.com/DrDecki/VHDB/releases/download/catalog/vhdb.vpk"


def fetch(url):
    result = subprocess.run(["curl", "-sL", "--max-time", "300", "--fail", url],
                            capture_output=True)
    if result.returncode != 0:
        return None
    return result.stdout


def main():
    if len(sys.argv) != 2:
        print("usage: update_self_entry.py <version>")
        return 1
    version = sys.argv[1]

    blob = fetch(VPK_URL)
    if blob is None:
        print("could not download %s" % VPK_URL)
        return 1

    vpk_hash = hashlib.md5(blob).hexdigest()

    with zipfile.ZipFile(io.BytesIO(blob)) as archive:
        eboot = archive.read("eboot.bin")
    eboot_hash = hashlib.md5(eboot).hexdigest()

    entry = {
        "id": str(SELF_ID),
        "name": "VHDB",
        "author": "DrDecki",
        "version": version,
        "type": "4",
        "titleid": SELF_TITLEID,
        "date": date.today().isoformat(),
        "size": str(len(blob)),
        "hash": vpk_hash,
        "url": VPK_URL,
        "description": "The store you are using right now.",
        "long_description": "The store you are using right now. Installing "
                             "this update replaces it with the newest build.",
        "source": "https://github.com/DrDecki/VHDB",
        "release_page": "https://github.com/DrDecki/VHDB/releases/tag/catalog",
        "icon": "", "screenshots": "", "trailer": "", "requirements": "",
        "data": "", "data_size": "0", "hash2": "", "trophies": "0",
    }

    with open(os.path.join(REPO, "client_entry.json"), "w", encoding="utf-8") as handle:
        json.dump(entry, handle, indent=1)

    cache_path = os.path.join(REPO, "eboot_hashes.json")
    cache = {}
    if os.path.exists(cache_path):
        with open(cache_path, "r", encoding="utf-8") as handle:
            cache = json.load(handle)
    cache[str(SELF_ID)] = {"vpk": vpk_hash, "eboot": eboot_hash, "aux": "",
                           "aux_kind": 0}
    with open(cache_path, "w", encoding="utf-8") as handle:
        json.dump(cache, handle, indent=1, sort_keys=True)

    print("version      %s" % version)
    print("vpk size     %d bytes" % len(blob))
    print("vpk hash     %s" % vpk_hash)
    print("eboot hash   %s" % eboot_hash)
    print("wrote client_entry.json and updated eboot_hashes.json")
    return 0


if __name__ == "__main__":
    sys.exit(main())
