#!/usr/bin/env python3
"""Replay a recorded Hearthstone log session into a tracker build, without Hearthstone.

The tracker runs isolated: no windows (Qt offscreen platform, so no screen capture either),
its own data dir (a fake HOME) and its own QSettings domain (AT_SETTINGS_APP). The session's log lines are appended in timestamp order to a fake Logs dir, faster
than real time, the way Hearthstone writes them. What the tracker did is in <out>/home/Arena Dude
(ArenaDudeLog.txt, Arena Stats, ArenaDudeDrafts.json).

Only the log-driven half is exercised: deck, draft start/end, run record, the mascot's game lines.
The screen recognition (plates, OCR) sees nothing.

    tools/replay_session.py <app or binary> <session dir with *.log.gz> <out dir> [--speed 20]
"""

import argparse
import gzip
import os
import re
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path

REAL_DOMAIN = "com.arena-dude.Arena Dude"
COMPONENTS = ["LoadingScreen", "Power", "Zone", "Arena", "Asset"]
TIME_RE = re.compile(r"^[DIWE] (\d\d):(\d\d):(\d\d)\.(\d{7}) ")
#Folders of the real data dir worth cloning: without them the tracker downloads every card image
DATA_DIRS = ["Extra", "Histograms", "Hearthstone Cards", "HD Images"]


#Log lines that end the tracker's startup
STARTUP_READY = ["Fire cards (WR/Samples) ready.", "MainWindow: Mascot: "]


def wait_for(log, patterns, timeout, proc):
    end = time.time() + timeout
    while time.time() < end:
        if proc.poll() is not None:
            return False
        text = log.read_text(errors="replace") if log.exists() else ""
        if all(p in text for p in patterns):
            return True
        time.sleep(1)
    return False


def wait_quiet(log, quiet, timeout):
    """Until the log hasn't grown for quiet seconds."""
    end = time.time() + timeout
    size, since = -1, time.time()
    while time.time() < end:
        now = log.stat().st_size if log.exists() else 0
        if now != size:
            size, since = now, time.time()
        elif time.time() - since >= quiet:
            return
        time.sleep(1)


def binary_of(app):
    app = Path(app)
    if app.suffix == ".app":
        return next((app / "Contents" / "MacOS").iterdir())
    return app


def read_session(session):
    """[(time in 100 ns, order, component, line)] of every component log, continuations kept with their line."""
    events = []
    order = 0
    for comp in COMPONENTS:
        path = Path(session) / (comp + ".log.gz")
        if not path.exists():
            path = Path(session) / (comp + ".log")
            if not path.exists():
                continue
        opener = gzip.open if path.suffix == ".gz" else open
        day = 0
        last = None
        with opener(path, "rt", encoding="utf-8", errors="replace") as f:
            for line in f:
                m = TIME_RE.match(line)
                if m:
                    t = ((int(m[1]) * 60 + int(m[2])) * 60 + int(m[3])) * 10**7 + int(m[4])
                    if last is not None and t + 12 * 3600 * 10**7 < last:   #Past midnight
                        day += 1
                    last = t
                    t += day * 24 * 3600 * 10**7
                elif last is None:
                    continue
                else:
                    t = last + day * 24 * 3600 * 10**7
                events.append((t, order, comp, line))
                order += 1
    events.sort(key=lambda e: (e[0], e[1]))
    return events


def setup(out, tag):
    out = Path(out)
    if out.exists():
        shutil.rmtree(out)
    home = out / "home"
    data = home / "Arena Dude"
    (data / "Arena Stats").mkdir(parents=True)
    real = Path.home() / "Arena Dude"
    for d in DATA_DIRS:
        if (real / d).exists():
            subprocess.run(["cp", "-cR", str(real / d), str(data / d)], check=True)    #APFS clone: instant

    hsprefs = home / "Library" / "Preferences" / "Blizzard" / "Hearthstone"
    hsprefs.mkdir(parents=True)
    real_config = Path.home() / "Library/Preferences/Blizzard/Hearthstone/log.config"
    if real_config.exists():
        shutil.copy(real_config, hsprefs / "log.config")

    logs = out / "Logs" / "Hearthstone_replay"
    logs.mkdir(parents=True)
    for comp in COMPONENTS:
        (logs / (comp + ".log")).touch()

    #The real settings (versions of the data, arena sets...) with the paths moved to the fake dirs
    domain = "com.arena-dude." + tag
    subprocess.run(["defaults", "delete", domain], capture_output=True)
    exported = subprocess.run(["defaults", "export", REAL_DOMAIN, "-"], capture_output=True, check=True).stdout
    subprocess.run(["defaults", "import", domain, "-"], input=exported, check=True)
    subprocess.run(["defaults", "write", domain, "logsDirPath", "-string", str(out / "Logs")], check=True)
    subprocess.run(["defaults", "write", domain, "logConfig", "-string", str(hsprefs / "log.config")], check=True)
    return home, logs, domain


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("app")
    ap.add_argument("session")
    ap.add_argument("out")
    ap.add_argument("--speed", type=float, default=20, help="log seconds per real second")
    ap.add_argument("--max-gap", type=float, default=2, help="longest real wait between lines, seconds")
    ap.add_argument("--tag", default="Arena Dude Test", help="QSettings application name of the run")
    args = ap.parse_args()

    events = read_session(args.session)
    if not events:
        sys.exit("No log lines in " + args.session)
    home, logs, domain = setup(args.out, args.tag)

    env = dict(os.environ, HOME=str(home), AT_SETTINGS_APP=args.tag, QT_QPA_PLATFORM="offscreen")
    stdout = open(Path(args.out) / "stdout.txt", "w")
    proc = subprocess.Popen([str(binary_of(args.app))], env=env, stdout=stdout, stderr=subprocess.STDOUT)
    log = home / "Arena Dude" / "ArenaDudeLog.txt"
    try:
        #Startup: loading the winrates takes a while; the greeting comes when the logs are caught up
        if not wait_for(log, STARTUP_READY, 300, proc):
            sys.exit("The tracker didn't finish its startup, see " + str(log))
        files = {c: open(logs / (c + ".log"), "a", encoding="utf-8") for c in COMPONENTS}
        prev = events[0][0]
        start = time.time()
        i = 0
        while i < len(events):
            t = events[i][0]
            wait = min((t - prev) / 10**7 / args.speed, args.max_gap)
            if wait > 0.001:
                for f in files.values():
                    f.flush()
                time.sleep(wait)
            #Every line with this timestamp at once, as Hearthstone writes a burst
            while i < len(events) and events[i][0] == t:
                files[events[i][2]].write(events[i][3])
                i += 1
            prev = t
            if proc.poll() is not None:
                sys.exit("The tracker exited during the replay, see " + str(Path(args.out) / "stdout.txt"))
        for f in files.values():
            f.close()
        wait_quiet(log, 30, 300)     #Timed reactions after the last line, and a tracker behind the replay
        print(f"Replayed {len(events)} lines in {time.time() - start:.0f} s")
    finally:
        proc.send_signal(signal.SIGKILL)
        proc.wait()
        stdout.close()
        subprocess.run(["defaults", "delete", domain], capture_output=True)


if __name__ == "__main__":
    main()
