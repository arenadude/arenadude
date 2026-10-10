#!/usr/bin/env python3
"""Replay a recorded Hearthstone log session into a tracker build, without Hearthstone.

The tracker runs isolated: no windows (Qt offscreen platform, so no screen capture either),
its own data dir (a fake HOME) and its own QSettings domain (AT_SETTINGS_APP). The session's log lines are appended in timestamp order to a fake Logs dir, faster
than real time, the way Hearthstone writes them. What the tracker did is in <out>/home/Arena Dude
(ArenaDudeLog.txt, Arena Stats, ArenaDudeDrafts.json).

Only the log-driven half is exercised: deck, draft start/end, run record, the mascot's game lines.
The screen recognition (plates, OCR) sees nothing, unless the fixture has screen recordings: with --screen
<fixture>/screen (screen.json and its clips) the tracker's screen is the recording's frame of the replay's time.
The recording shows the tracker of that session too: "hide": ["mascotBubble"] in screen.json paints its speech bubble
black, as that tracker did before reading the screen (Python's cv2 needed).
Inside a clip the replay goes at real time, the tracker's loops need it; between clips at --speed.

    tools/replay_session.py <app or binary> <session dir with *.log.gz> <out dir> [--speed 20] [--screen <dir>]
"""

import argparse
import gzip
import json
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


def hide_mascot_bubble(path):
    """Paints the recorded tracker's speech bubble black, as the live tracker did before reading the screen: it lists
    card names. The bubble is the biggest pure white area in the left third (Hearthstone has none that big)."""
    import cv2
    img = cv2.imread(str(path))
    h, w = img.shape[:2]
    white = cv2.inRange(img[:, :w // 3], (240, 240, 240), (255, 255, 255))
    n, _, stats, _ = cv2.connectedComponentsWithStats(white, connectivity=4)
    if n < 2:
        return
    x, y, bw, bh, area = max(stats[1:], key=lambda s: s[cv2.CC_STAT_AREA])
    if area < 0.0015 * w * h or bw < 0.05 * w:     #The cursor's glove, white too, is smaller
        return
    m = int(0.006 * w)
    cv2.rectangle(img, (int(x) - m, int(y) - m), (int(x + bw) + m, int(y + bh) + m), (0, 0, 0), -1)
    cv2.imwrite(str(path), img, [cv2.IMWRITE_JPEG_QUALITY, 95])


class Screen:
    """The recorded screen: clips extracted to frames, the current one copied to frame.jpg for the tracker."""
    FPS = 5

    def __init__(self, screen_dir, out, first_time):
        screen_dir = Path(screen_dir)
        self.config = json.loads((screen_dir / "screen.json").read_text())
        self.frame = Path(out) / "frame.jpg"
        self.current = None
        self.clips = []     #(start, end, frames dir, number of frames), times as the events'
        for clip in self.config["clips"]:
            h, m, s = (int(x) for x in clip["start"].split(":"))
            start = ((h * 60 + m) * 60 + s) * 10**7
            if start + 12 * 3600 * 10**7 < first_time:      #After the session's midnight
                start += 24 * 3600 * 10**7
            frames = Path(out) / "frames" / Path(clip["file"]).stem
            frames.mkdir(parents=True)
            print("Extracting the frames of " + clip["file"] + "...")
            subprocess.run(["ffmpeg", "-v", "error", "-i", str(screen_dir / clip["file"]), "-vf", f"fps={self.FPS}",
                            "-q:v", "2", str(frames / "%06d.jpg")], check=True)
            if "mascotBubble" in self.config.get("hide", []):
                from concurrent.futures import ThreadPoolExecutor
                with ThreadPoolExecutor(8) as pool:
                    list(pool.map(hide_mascot_bubble, sorted(frames.iterdir())))
            count = len(list(frames.iterdir()))
            self.clips.append((start, start + count * 10**7 // self.FPS, frames, count))

    def platform(self, out):
        """The offscreen platform with the recorded screen's size and pixel ratio."""
        screen = self.config["screen"]
        config = Path(out) / "screen-platform.json"
        config.write_text(json.dumps({"screens": [{"name": "replay", "x": 0, "y": 0, "width": screen["width"],
                                                   "height": screen["height"], "logicalDpi": 72, "logicalBaseDpi": 72,
                                                   "dpr": screen["dpr"]}]}))
        return "offscreen:configfile=" + str(config)

    def env(self):
        return {"AT_REPLAY_FRAME": str(self.frame), "AT_REPLAY_HS_RECT": ",".join(str(v) for v in self.config["hsRect"])}

    def clip_at(self, t):
        return next((clip for clip in self.clips if clip[0] <= t < clip[1]), None)

    def next_start(self, t):
        return min((clip[0] for clip in self.clips if clip[0] > t), default=None)

    def show(self, t):
        """The frame of time t, or none outside the clips."""
        clip = self.clip_at(t)
        path = None
        if clip is not None:
            path = clip[2] / ("%06d.jpg" % min(clip[3], 1 + (t - clip[0]) * self.FPS // 10**7))
        if path == self.current:
            return
        self.current = path
        if path is None:
            self.frame.unlink(missing_ok=True)
        else:
            #A new file (a copy, not a rename) gets a new modification time: that's how the tracker sees the change
            tmp = self.frame.with_suffix(".tmp.jpg")
            shutil.copyfile(path, tmp)
            os.replace(tmp, self.frame)


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
    ap.add_argument("--screen", help="the fixture's screen dir (screen.json and its clips): the tracker sees them")
    args = ap.parse_args()

    events = read_session(args.session)
    if not events:
        sys.exit("No log lines in " + args.session)
    home, logs, domain = setup(args.out, args.tag)
    screen = Screen(args.screen, args.out, events[0][0]) if args.screen else None

    env = dict(os.environ, HOME=str(home), AT_SETTINGS_APP=args.tag, QT_QPA_PLATFORM="offscreen")
    if screen is not None:
        env.update(screen.env(), QT_QPA_PLATFORM=screen.platform(args.out))
    stdout = open(Path(args.out) / "stdout.txt", "w")
    proc = subprocess.Popen([str(binary_of(args.app))], env=env, stdout=stdout, stderr=subprocess.STDOUT)
    log = home / "Arena Dude" / "ArenaDudeLog.txt"
    try:
        #Startup: loading the winrates takes a while; the greeting comes when the logs are caught up
        if not wait_for(log, STARTUP_READY, 300, proc):
            sys.exit("The tracker didn't finish its startup, see " + str(log))
        files = {c: open(logs / (c + ".log"), "a", encoding="utf-8") for c in COMPONENTS}
        clock = events[0][0]    #The replay's time, as the events'
        start = time.time()
        i = 0
        while i < len(events):
            t = events[i][0]
            if t > clock:
                for f in files.values():
                    f.flush()
            while clock < t:
                clip = screen.clip_at(clock) if screen else None
                if clip is not None:
                    #Real time, the frames following
                    target = min(t, clip[1])
                    before = time.time()
                    time.sleep(min(0.05, (target - clock) / 10**7))
                    clock = min(target, clock + int((time.time() - before) * 10**7))
                else:
                    next_start = screen.next_start(clock) if screen else None
                    target = t if next_start is None else min(t, next_start)
                    wait = min((target - clock) / 10**7 / args.speed, args.max_gap)
                    if wait > 0.001:
                        time.sleep(wait)
                    clock = target
                if screen:
                    screen.show(clock)
            #Every line with this timestamp at once, as Hearthstone writes a burst
            while i < len(events) and events[i][0] == t:
                files[events[i][2]].write(events[i][3])
                i += 1
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
