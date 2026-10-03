#!/usr/bin/env python3
"""Compare what two runs of tools/replay_session.py did with the same session (e.g. the last commit and a change).

    tools/compare_replays.py <old out dir> <new out dir>

Compared: the deck/draft/run events of the tracker log, the game detection lines (player, first player, winner,
enemy secrets), ArenaDudeStats.json (without its dates: they are the time of the replay) and
ArenaDudeDrafts.json. The mascot's lines are listed side by side: it picks them at random, so compare the
outcomes (win/loss lines in the same order), not the texts. Exits with 1 if anything compared differs.
"""
import difflib
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
MASCOT = "MainWindow: Mascot: "


def read_log(run):
    """Events, game lines and mascot lines of a run, without timestamps and log line numbers."""
    events, games, mascot = [], [], []
    log = Path(run) / "home/Arena Dude/ArenaDudeLog.txt"
    for line in log.read_text(errors="replace").splitlines():
        body = line[11:] if re.match(r"\d\d:\d\d:\d\d - ", line) else line
        body = re.sub(r"GameWatcher\(\d+\)", "GameWatcher", body)
        if body.startswith(MASCOT):
            mascot.append(body[len(MASCOT):])
        elif any(re.fullmatch(p, body) for p in EVENTS):
            events.append(body)
        elif any(re.fullmatch(p, body) for p in GAMES):
            games.append(body)
    return events, games, mascot


def read_json(run, name, drop_dates=False):
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
    eo, go, mo = read_log(old)
    en, gn, mn = read_log(new)
    ok = compare("Deck/draft/run events", eo, en)
    ok &= compare("Game detection lines", go, gn)
    ok &= compare("ArenaDudeStats.json", read_json(old, "Arena Stats/ArenaDudeStats.json", True),
                  read_json(new, "Arena Stats/ArenaDudeStats.json", True))
    ok &= compare("ArenaDudeDrafts.json", read_json(old, "ArenaDudeDrafts.json"), read_json(new, "ArenaDudeDrafts.json"))

    print(f"\nMascot lines: {len(mo)} old, {len(mn)} new")
    for a, b in itertools.zip_longest(mo, mn, fillvalue=""):
        print(f"  {a[:75]:77} | {b[:75]}")
    print("\nSAME" if ok else "\nDIFFERENT")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
