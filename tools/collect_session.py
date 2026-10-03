#!/usr/bin/env python3
"""Archives a play session as a test fixture: Hearthstone logs, the tracker's own log and state,
screenshots and a note about what happened.

    tools/collect_session.py [-n "note"] [-c] [-s file ...] [--all] [--out DIR]

    -n, --note       what you did and what went wrong (asked interactively when omitted)
    -c, --clipboard  also save the image currently on the clipboard (macOS screenshots to clipboard)
    -s, --screens    screenshot files to copy into the fixture
    --all            copy every Hearthstone log session, not only the ones changed since the last capture
    --out            fixtures root (default: ~/Desktop/hs fixtures, or $AT_FIXTURES)

Each run creates <root>/<YYYY-MM-DD_HHMM>_<slug>/ with note.md, hs-logs/<session>/*.log.gz,
tracker/ and screens/. Hearthstone keeps only its last few log sessions, so run this after every
session you may want to replay later. The logs contain BattleTags: keep fixtures out of the
public repo.
"""
import argparse
import datetime
import glob
import gzip
import os
import re
import shutil
import subprocess
import sys

HOME = os.path.expanduser("~")
TRACKER_DIR = os.path.join(HOME, "Arena Dude")
TRACKER_FILES = ["ArenaDudeLog.txt", "ArenaDudeLog.old", "ArenaDudeDrafts.json"]
DEFAULT_HS_LOGS = "/Applications/Hearthstone/Logs"
LAST_CAPTURE = ".last_capture"


def hs_logs_dir():
    # The tracker stores the logs dir it found in its QSettings
    try:
        path = subprocess.run(["defaults", "read", "com.arena-dude.Arena Dude", "logsDirPath"],
                              capture_output=True, text=True).stdout.strip()
    except OSError:
        path = ""
    return path if path and os.path.isdir(path) else DEFAULT_HS_LOGS


def newest_mtime(path):
    times = [os.path.getmtime(f) for f in glob.glob(os.path.join(path, "*"))]
    return max(times, default=os.path.getmtime(path))


def pick_sessions(logs_dir, since, take_all):
    sessions = sorted(d for d in glob.glob(os.path.join(logs_dir, "Hearthstone_*")) if os.path.isdir(d))
    if take_all or not sessions:
        return sessions
    changed = [d for d in sessions if newest_mtime(d) > since]
    return changed or sessions[-1:]


def gzip_copy(src, dst):
    with open(src, "rb") as fin, gzip.open(dst + ".gz", "wb") as fout:
        shutil.copyfileobj(fin, fout)


def save_clipboard_image(dst):
    script = ['set f to open for access POSIX file "%s" with write permission' % dst,
              'write (the clipboard as «class PNGf») to f',
              'close access f']
    args = ["osascript"]
    for line in script:
        args += ["-e", line]
    ok = subprocess.run(args, capture_output=True).returncode == 0
    if not ok or os.path.getsize(dst) == 0:
        if os.path.exists(dst):
            os.remove(dst)
        return False
    return True


def running_tracker():
    out = subprocess.run(["ps", "-axo", "command"], capture_output=True, text=True).stdout
    apps = sorted({m.group(1) for m in re.finditer(r"/([^/]*\.app)/Contents/MacOS/", out) if "ArenaDude" in m.group(1)})
    return ", ".join(apps) or "not running"


def slugify(text):
    slug = re.sub(r"[^\w]+", "-", text.lower(), flags=re.UNICODE).strip("-")
    return slug[:40] or "session"


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("-n", "--note")
    parser.add_argument("-c", "--clipboard", action="store_true")
    parser.add_argument("-s", "--screens", nargs="*", default=[])
    parser.add_argument("--all", action="store_true")
    parser.add_argument("--out", default=os.environ.get("AT_FIXTURES", os.path.join(HOME, "Desktop", "hs fixtures")))
    args = parser.parse_args()

    note = args.note
    if note is None:
        note = input("What happened (one line, empty to skip): ").strip() if sys.stdin.isatty() else ""

    root = args.out
    os.makedirs(root, exist_ok=True)
    marker = os.path.join(root, LAST_CAPTURE)
    since = os.path.getmtime(marker) if os.path.exists(marker) else 0

    now = datetime.datetime.now()
    base = os.path.join(root, "%s_%s" % (now.strftime("%Y-%m-%d_%H%M"), slugify(note)))
    fixture, suffix = base, 2
    while os.path.exists(fixture):
        fixture, suffix = "%s_%d" % (base, suffix), suffix + 1
    os.makedirs(fixture)

    # Hearthstone logs
    logs_dir = hs_logs_dir()
    sessions = pick_sessions(logs_dir, since, args.all)
    for session in sessions:
        dst = os.path.join(fixture, "hs-logs", os.path.basename(session))
        os.makedirs(dst)
        for log in glob.glob(os.path.join(session, "*.log")):
            if os.path.getsize(log) > 0:
                gzip_copy(log, os.path.join(dst, os.path.basename(log)))

    # Tracker log and state
    tracker_dst = os.path.join(fixture, "tracker")
    os.makedirs(tracker_dst)
    for name in TRACKER_FILES:
        src = os.path.join(TRACKER_DIR, name)
        if not os.path.exists(src):
            continue
        if name.endswith(".json"):
            shutil.copy2(src, tracker_dst)
        else:
            gzip_copy(src, os.path.join(tracker_dst, name))
    stats = os.path.join(TRACKER_DIR, "Arena Stats")
    if os.path.isdir(stats):
        shutil.copytree(stats, os.path.join(tracker_dst, "Arena Stats"),
                        ignore=shutil.ignore_patterns(".DS_Store"))

    # Screenshots
    screens = []
    screens_dst = os.path.join(fixture, "screens")
    os.makedirs(screens_dst)
    if args.clipboard:
        dst = os.path.join(screens_dst, "clipboard_%s.png" % now.strftime("%H%M%S"))
        if save_clipboard_image(dst):
            screens.append(os.path.basename(dst))
        else:
            print("No image on the clipboard.")
    for src in args.screens:
        shutil.copy2(src, screens_dst)
        screens.append(os.path.basename(src))

    with open(os.path.join(fixture, "note.md"), "w") as f:
        f.write("# %s\n\n" % (note or "Session"))
        f.write("- Captured: %s\n" % now.strftime("%Y-%m-%d %H:%M"))
        f.write("- Tracker build: %s\n" % running_tracker())
        f.write("- HS log sessions: %s\n" % (", ".join(os.path.basename(s) for s in sessions) or "none"))
        f.write("- Screens: %s\n\n" % (", ".join(screens) or "none"))
        f.write("## Expected\n\n\n## Actual\n\n")

    open(marker, "w").close()
    size = sum(os.path.getsize(os.path.join(d, n)) for d, _, files in os.walk(fixture) for n in files)
    print("Fixture: %s (%.1f MB)" % (fixture, size / 1e6))
    print("HS sessions: %d, screens: %d" % (len(sessions), len(screens)))


if __name__ == "__main__":
    main()
