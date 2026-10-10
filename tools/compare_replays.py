#!/usr/bin/env python3
"""Compare what two runs of tools/replay_session.py did with the same session (e.g. the last commit and a change).

    tools/compare_replays.py <old out dir> <new out dir>

Compared: the deck/draft/run events of the tracker log, the game detection lines (player, first player, winner,
enemy secrets), ArenaDudeStats.json (without its dates: they are the time of the replay) and
ArenaDudeDrafts.json. The mascot's lines are listed side by side: it picks them at random, so compare the
outcomes (win/loss lines in the same order), not the texts. Exits with 1 if anything compared differs.

Runs with recorded screens (replay_session.py --screen) also compare what the screen recognition read: the heroes
and cards of each pick, the legendary bundles, the redraft's discards. Either side can be a tracker log instead
of a run (ArenaDudeLog.txt or .gz, e.g. the fixture's log of the live session): then only the logs are compared.
The plates' places are listed, not compared (a pixel off is no difference), and a golden card is the same card.
"""
import difflib
import gzip
import itertools
import json
import re
import sys
from pathlib import Path

EVENTS = [
    r"DeckHandler: Add to deck: .*",
    r"DeckHandler: Deck snapshot sync: .*",
    r"DeckHandler: Completing Arena Deck: .*",
    r"DeckHandler: Save draft deck.*",
    r"DeckHandler: Delete draft deck.*",
    r"DeckHandler: Redraft review deck.*",
    r"DeckHandler: (Enter|Leave) arena",
    r"DeckHandler: Deck list cleared\.",
    r"DraftHandler: Begin draft\. Hero: \d+",
    r"DraftHandler: Begin hero draft\.",
    r"DraftHandler: End (hero )?draft\.",
    r"DraftHandler: Counters starts with \d+ cards\.",
    r"DraftHandler: Pick card: \w+",
    r"DraftHandler: Bundle picked: .*",
    r"GameWatcher: (Found SetDraftMode - \w+|Pick card: \w+)",
    r"ArenaHandler: .*",
]
GAMES = [
    r"GameWatcher: Found (WON|TIED).*",
    r"GameWatcher: Found playerID: .*",
    r"GameWatcher: Found playerTag: .*",
    r"GameWatcher: Found First Player: .*",
    r"GameWatcher: Enemy: Secret played.*",
]
SCREEN = [
    r"DraftHandler: Bundle of .*",
    r"DraftHandler: Redraft review picks: .*",
]
CHOOSE = re.compile(r"DraftHandler: Choose: (\w+) ")
PLATES = re.compile(r"DraftScoreWindow: Plates by names: .*")
MASCOT = "MainWindow: Mascot: "


def log_path(run):
    run = Path(run)
    return run if run.is_file() else run / "home/Arena Dude/ArenaDudeLog.txt"


def read_log(run):
    """Events, game lines, mascot lines, screen readings and plates of a run, without timestamps and log line numbers.
    The screen readings have each pick's heroes or cards once, as "Pick: A / B / C"."""
    events, games, mascot, screen, plates = [], [], [], [], []
    path = log_path(run)
    text = gzip.open(path, "rt", errors="replace").read() if path.suffix == ".gz" else path.read_text(errors="replace")
    chosen = []
    for line in text.splitlines():
        body = line[11:] if re.match(r"\d\d:\d\d:\d\d - ", line) else line
        body = re.sub(r"GameWatcher\(\d+\)", "GameWatcher", body)
        match = CHOOSE.match(body)
        if match:
            chosen.append(match[1].removesuffix("_premium"))   #Golden or not, the same card: video shifts colors
            if len(chosen) == 3:
                pick = "Pick: " + " / ".join(chosen)
                if not screen or screen[-1] != pick:
                    screen.append(pick)
                chosen = []
            continue
        if body.startswith(MASCOT):
            mascot.append(body[len(MASCOT):])
        elif any(re.fullmatch(p, body) for p in EVENTS):
            events.append(body)
        elif any(re.fullmatch(p, body) for p in GAMES):
            games.append(body)
        elif any(re.fullmatch(p, body) for p in SCREEN):
            screen.append(body)
        elif PLATES.fullmatch(body):
            plates.append(body[len("DraftScoreWindow: "):])
    return events, games, mascot, screen, plates


def read_json(run, name, drop_dates=False):
    if Path(run).is_file():
        return None
    path = Path(run) / "home/Arena Dude" / name
    if not path.exists():
        return None
    data = json.loads(path.read_text())
    if drop_dates and isinstance(data, dict):
        #Runs are archived by date and the last game has its time: both are the replay's
        data = {k: v for k, v in data.items() if not re.match(r"\d{4}\.\d\d\.\d\d", k)}
        data.get("extra", {}).pop("lastGame", None)
    return data


def compare(title, old, new):
    if old == new:
        print(f"{title}: same ({len(old) if isinstance(old, list) else 'json'})")
        return True
    print(f"{title}: DIFFERENT")
    if isinstance(old, list):
        for line in difflib.unified_diff(old, new, "old", "new", lineterm="", n=1):
            print("  " + line)
    else:
        print("  old:", json.dumps(old)[:1500])
        print("  new:", json.dumps(new)[:1500])
    return False


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    old, new = sys.argv[1], sys.argv[2]
    eo, go, mo, so, po = read_log(old)
    en, gn, mn, sn, pn = read_log(new)
    ok = compare("Deck/draft/run events", eo, en)
    ok &= compare("Game detection lines", go, gn)
    if so or sn:
        ok &= compare("Screen readings", so, sn)
    if not (Path(old).is_file() or Path(new).is_file()):
        ok &= compare("ArenaDudeStats.json", read_json(old, "Arena Stats/ArenaDudeStats.json", True),
                      read_json(new, "Arena Stats/ArenaDudeStats.json", True))
        ok &= compare("ArenaDudeDrafts.json", read_json(old, "ArenaDudeDrafts.json"), read_json(new, "ArenaDudeDrafts.json"))

    if po or pn:
        print(f"\nPlates: {len(po)} old, {len(pn)} new")
        for a, b in itertools.zip_longest(po, pn, fillvalue=""):
            print(f"  {a[:75]:77} | {b[:75]}")

    print(f"\nMascot lines: {len(mo)} old, {len(mn)} new")
    for a, b in itertools.zip_longest(mo, mn, fillvalue=""):
        print(f"  {a[:75]:77} | {b[:75]}")
    print("\nSAME" if ok else "\nDIFFERENT")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
