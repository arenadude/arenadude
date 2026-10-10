#!/usr/bin/env python3
"""Checks a tracker build against every recorded play session (the fixtures), before it's handed to anyone.

    tools/check_build.py --out DIR [--baseline DIR] [--app APP] [--asan] [--only TEXT] [--jobs 3]

    --out       where the build, the runs and the report go (a later check can use it as its baseline)
    --baseline  the --out of an earlier check (the last good build): each run is compared with its run there
    --app       check this .app instead of building the working tree
    --asan      also build with AddressSanitizer/UBSan and replay the log-only sessions with it (slower, finds
                memory errors: use-after-free, out of bounds, threads racing over a container)
    --only      only the sessions whose fixture or session name contains TEXT
    --fixtures  fixtures root (default: ~/Desktop/hs fixtures, or $AT_FIXTURES)

Every Hearthstone log session of the fixtures is replayed once (tools/replay_session.py), the sessions without
screen recordings 3 at a time, the ones with a <fixture>/screen recording alone and at --screen-speed (their clips
go at real time and the tracker's loops need the machine). Each run is compared with tools/compare_replays.py: with
the baseline's run of the same session, and for screen sessions with the live tracker log of the fixture too
(listed only: it has known differences). report.txt in --out has it all; the exit code is 1 when a run crashed,
a sanitizer found an error or a run differs from the baseline.

A full check takes about an hour (the screen sessions are real time). Without --baseline it only looks for crashes
and sanitizer errors: run it on the last good build first (--app) to make one.
"""
import argparse
import contextlib
import io
import os
import re
import shutil
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
import compare_replays     # noqa: E402
import replay_session      # noqa: E402

QMAKE = shutil.which("qmake") or "/opt/homebrew/bin/qmake"
ASAN_FLAGS = ["CONFIG+=debug", "CONFIG-=release",
              "QMAKE_CXXFLAGS+=-fsanitize=address,undefined -fno-omit-frame-pointer",
              "QMAKE_OBJECTIVE_CFLAGS+=-fsanitize=address,undefined", "QMAKE_LFLAGS+=-fsanitize=address,undefined"]
SANITIZER_RE = re.compile(r"AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer")


def build(build_dir, extra=()):
    build_dir.mkdir(parents=True, exist_ok=True)
    print(f"Building in {build_dir}...", flush=True)
    log = open(build_dir / "build.txt", "w")
    subprocess.run([QMAKE, str(REPO / "ArenaDude.pro"), *extra], cwd=build_dir, stdout=log, stderr=subprocess.STDOUT,
                   check=True)
    result = subprocess.run(["make", f"-j{os.cpu_count()}"], cwd=build_dir, stdout=log, stderr=subprocess.STDOUT)
    if result.returncode != 0:
        sys.exit(f"Build failed, see {build_dir / 'build.txt'}")
    return build_dir / "ArenaDude.app"


def find_sessions(fixtures, only):
    """[(name, session dir, fixture dir, screen dir or None)], each Hearthstone session once: the copy with the most
    logs, and the screen recording of a fixture for the session its clips are in."""
    copies = {}
    for session in sorted(fixtures.glob("*/hs-logs/*/")):
        fixture = session.parent.parent
        if only and only not in fixture.name and only not in session.name:
            continue
        screen = fixture / "screen"
        if not (screen / "screen.json").exists() or not clips_in(screen, session):
            screen = None
        best = copies.get(session.name)
        rank = (screen is not None, len(list(session.iterdir())))
        if best is None or rank > best[0]:
            copies[session.name] = (rank, session, fixture, screen)
    return [(name, s, f, sc) for name, (_, s, f, sc) in sorted(copies.items())]


def clips_in(screen, session):
    """A clip of the recording starts inside the session's log times."""
    import json
    events = replay_session.read_session(session)
    if not events:
        return False
    first, last = events[0][0], events[-1][0]
    for clip in json.loads((screen / "screen.json").read_text())["clips"]:
        h, m, s = (int(x) for x in clip["start"].split(":"))
        start = ((h * 60 + m) * 60 + s) * 10**7
        if start + 12 * 3600 * 10**7 < first:
            start += 24 * 3600 * 10**7
        if first <= start <= last:
            return True
    return False


def replay(app, session, run_dir, tag, speed, screen=None, asan=False):
    """Runs tools/replay_session.py; the result line and whether the tracker survived."""
    cmd = [sys.executable, str(REPO / "tools/replay_session.py"), str(app), str(session), str(run_dir),
           "--speed", str(speed), "--tag", tag]
    if screen is not None:
        cmd += ["--screen", str(screen)]
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="print_stacktrace=1") if asan else None
    started = time.time()
    result = subprocess.run(cmd, capture_output=True, text=True, env=env)
    out = (result.stdout + result.stderr).strip().splitlines()
    return {"ok": result.returncode == 0, "line": out[-1] if out else "", "minutes": (time.time() - started) / 60}


def sanitizer_errors(run_dir):
    stdout = run_dir / "stdout.txt"
    if not stdout.exists():
        return []
    return [line for line in stdout.read_text(errors="replace").splitlines() if SANITIZER_RE.search(line)]


def compare(old, new):
    """compare_replays' verdict and its report, without exiting."""
    report = io.StringIO()
    with contextlib.redirect_stdout(report):
        try:
            compare_replays.sys.argv = ["compare_replays.py", str(old), str(new)]
            compare_replays.main()
            same = True
        except SystemExit as e:
            same = (e.code == 0)
    return same, report.getvalue()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--out", required=True)
    ap.add_argument("--baseline")
    ap.add_argument("--app")
    ap.add_argument("--asan", action="store_true")
    ap.add_argument("--only")
    ap.add_argument("--jobs", type=int, default=3)
    ap.add_argument("--speed", type=float, default=10, help="log seconds per real second, log-only sessions")
    ap.add_argument("--screen-speed", type=float, default=5, help="the same between the clips of screen sessions")
    ap.add_argument("--fixtures", default=os.environ.get("AT_FIXTURES", str(Path.home() / "Desktop/hs fixtures")))
    args = ap.parse_args()

    out = Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    baseline = Path(args.baseline).resolve() if args.baseline else None
    sessions = find_sessions(Path(args.fixtures), args.only)
    if not sessions:
        sys.exit("No fixture sessions in " + args.fixtures)

    app = Path(args.app).resolve() if args.app else build(out / "build")
    asan_app = build(out / "build-asan", ASAN_FLAGS) if args.asan else None
    commit = subprocess.run(["git", "describe", "--always", "--dirty"], cwd=REPO, capture_output=True, text=True).stdout
    print(f"{len(sessions)} sessions, {sum(1 for s in sessions if s[3])} with screen recordings", flush=True)

    runs = {}

    def run_one(job):
        index, (name, session, fixture, screen), kind = job
        run_dir = out / kind / name
        target = asan_app if kind == "asan" else app
        speed = args.screen_speed if screen is not None and kind == "runs" else args.speed
        result = replay(target, session, run_dir, f"AD Check {kind} {index}", speed,
                        screen if kind == "runs" else None, asan=(kind == "asan"))
        result["sanitizer"] = sanitizer_errors(run_dir)
        runs[(kind, name)] = result
        print(f"  {kind:4} {name}: {result['line']} ({result['minutes']:.0f} min)", flush=True)

    log_jobs = [(i, s, "runs") for i, s in enumerate(sessions) if s[3] is None]
    log_jobs += [(i, s, "asan") for i, s in enumerate(sessions)] if asan_app else []
    screen_jobs = [(i, s, "runs") for i, s in enumerate(sessions) if s[3] is not None]
    print("Replaying the log-only sessions...", flush=True)
    with ThreadPoolExecutor(args.jobs) as pool:
        list(pool.map(run_one, log_jobs))
    print("Replaying the sessions with screen recordings, one at a time...", flush=True)
    for job in screen_jobs:
        run_one(job)

    #The report
    failed = False
    lines = [f"Check of {commit.strip()} ({app}), {time.strftime('%Y-%m-%d %H:%M')}",
             f"Baseline: {baseline or 'none (crashes and sanitizer errors only)'}", ""]
    details = []
    for name, session, fixture, screen in sessions:
        for kind in ("runs", "asan"):
            result = runs.get((kind, name))
            if result is None:
                continue
            status = []
            if not result["ok"]:
                status.append("CRASHED: " + result["line"])
            if result["sanitizer"]:
                status.append(f"SANITIZER: {len(result['sanitizer'])} lines, see {out / kind / name / 'stdout.txt'}")
            if kind == "runs" and baseline is not None:
                old = baseline / "runs" / name
                if old.exists():
                    same, report = compare(old, out / kind / name)
                    status.append("same as baseline" if same else "DIFFERENT from baseline")
                    if not same:
                        details += [f"===== {name} vs baseline", report]
                else:
                    status.append("no baseline run")
            if kind == "runs" and screen is not None:
                live = next(iter(sorted((fixture / "tracker").glob("ArenaDudeLog.txt*"))), None)
                if live is not None:
                    _, report = compare(live, out / kind / name)
                    details += [f"===== {name} vs the live log of {fixture.name} (known differences in its note)",
                                report]
            failed |= any(s.startswith(("CRASHED", "SANITIZER", "DIFFERENT")) for s in status)
            label = f"{name} [{'screen' if screen and kind == 'runs' else kind}]"
            lines.append(f"{label:52} {'; '.join(status) or 'ok'}")
    lines += ["", "RESULT: " + ("PROBLEMS" if failed else "OK"), ""]
    report = "\n".join(lines + details)
    (out / "report.txt").write_text(report)
    print("\n" + "\n".join(lines) + f"\nFull report: {out / 'report.txt'}")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
