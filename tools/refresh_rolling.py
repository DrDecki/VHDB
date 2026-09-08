import hashlib
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
CATALOG = os.environ.get("VHDB_CATALOG") or os.path.expanduser("~/vitadbtoo/VitaHomebrewDB")
STATE = os.path.join(REPO, "rolling_state.json")

SOURCES = ["apps.json", "psp_apps.json", os.path.join("preserved", "plugins.json"),
           os.path.join("preserved", "tools.json")]

ROLLING_URL = re.compile(r"/(continuous|nightly|latest|prerelease|ci|dev)/", re.I)
ROLLING_VERSION = re.compile(r"\b(nightly|continuous|latest)\b", re.I)


VERSIONED_NAME = re.compile(r"\d+[._]\d")


def is_rolling(url, version):
    if not url:
        return bool(version and ROLLING_VERSION.search(version))
    if "VitaHomebrewDB/releases/download/mirror/" in url:
        return False
    if VERSIONED_NAME.search(url.rsplit("/", 1)[-1]):
        return False
    if ROLLING_URL.search(url):
        return True
    return bool(version and ROLLING_VERSION.search(version))


def load_catalog():
    entries = []
    for name in SOURCES:
        path = os.path.join(CATALOG, name)
        if not os.path.exists(path):
            continue
        with open(path, "r", encoding="utf-8") as handle:
            data = json.load(handle)
        if isinstance(data, dict):
            data = list(data.values())
        entries.extend(data)
    return entries


def head(url):
    result = subprocess.run(["curl", "-sIL", "--max-time", "60", url],
                            capture_output=True, text=True)
    if result.returncode != 0:
        return {}
    fields = {}
    for line in result.stdout.splitlines():
        if ":" not in line:
            continue
        key, value = line.split(":", 1)
        key = key.strip().lower()
        if key in ("content-length", "etag", "last-modified"):
            fields[key] = value.strip()
    return fields


def fetch(url):
    result = subprocess.run(["curl", "-sL", "--max-time", "900", "--fail", url],
                            capture_output=True)
    if result.returncode != 0:
        return None
    return result.stdout


def main():
    state = {}
    if os.path.exists(STATE):
        with open(STATE, "r", encoding="utf-8") as handle:
            state = json.load(handle)

    fresh = 0
    unchanged = 0
    failed = 0

    for entry in load_catalog():
        app_id = str(entry.get("id") or "")
        url = (entry.get("url") or "").strip()
        if not app_id or not url:
            continue
        if not is_rolling(url, entry.get("version")):
            continue

        marks = head(url)
        stamp = marks.get("etag") or marks.get("last-modified") or ""
        size = marks.get("content-length") or ""
        known = state.get(app_id, {})

        if known.get("stamp") == stamp and known.get("size") == size and known.get("md5"):
            unchanged += 1
            continue

        blob = fetch(url)
        if blob is None:
            failed += 1
            print("failed     %-6s %s" % (app_id, entry.get("name")))
            continue

        state[app_id] = {
            "url": url,
            "size": str(len(blob)),
            "md5": hashlib.md5(blob).hexdigest(),
            "stamp": stamp,
        }
        fresh += 1
        print("refreshed  %-6s %-32s %s  %.1f MB" %
              (app_id, str(entry.get("name"))[:32], state[app_id]["md5"][:8],
               len(blob) / 1048576.0))

    with open(STATE, "w", encoding="utf-8") as handle:
        json.dump(state, handle, indent=1, sort_keys=True)

    print("")
    print("refreshed %d, unchanged %d, failed %d, tracking %d rolling builds" %
          (fresh, unchanged, failed, len(state)))


if __name__ == "__main__":
    main()
    sys.exit(0)
