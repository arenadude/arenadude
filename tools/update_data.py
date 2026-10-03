#!/usr/bin/env python3
"""Refresh the data files that Arena Dude downloads from this repo.

- CardsJson/cards.json        <- HearthstoneJSON (bumps cardsVersion.json when it changes)
- Arena/arenaVersion.json     <- arena sets derived from Firestone arena card stats
                                 (bumps arenaVersion when the set list changes)
- HearthArena/hearthArena.json <- heartharena.com tier list scores per class (bumps haVersion.json)

The card images are not here: the app downloads them from HearthstoneJSON and Hearthpwn (HSCardDownloader).

Usage:  python3 tools/update_data.py [--dry-run] [--sets SET1,SET2,...]
"""

import argparse
import gzip
import json
import re
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HSJ_CARDS_URL = "https://api.hearthstonejson.com/v1/latest/all/cards.json"
HEARTHARENA_TIERLIST_URL = "https://www.heartharena.com/tierlist"
FIRE_GLOBAL_URL = "https://static.zerotoheroes.com/api/arena/stats/cards/arena-underground/last-patch/global.gz.json"
USER_AGENT = "ArenaDude-data-updater (+https://github.com/arenadude/arenadude)"

# A set is in the arena pool when at least this fraction of its collectible cards shows up
# in Firestone's arena pick stats. Pool sets sit near 100%; discovered/generated cards from
# other sets only add a handful of entries per set.
ARENA_SET_MIN_COVERAGE = 0.5


def fetch(url, timeout=120):
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT, "Accept-Encoding": "gzip"})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        data = resp.read()
        if resp.headers.get("Content-Encoding") == "gzip" or data[:2] == b"\x1f\x8b":
            data = gzip.decompress(data)
        return data


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path, obj, dry_run):
    print(f"  write {path.relative_to(ROOT)}")
    if not dry_run:
        path.write_text(json.dumps(obj, indent=2) + "\n", encoding="utf-8")


def update_cards_json(dry_run):
    print("cards.json")
    data = fetch(HSJ_CARDS_URL)
    cards = json.loads(data)
    path = ROOT / "CardsJson" / "cards.json"
    if path.exists() and path.read_bytes() == data:
        print("  up to date")
        return cards

    old_ids = {c["id"] for c in read_json(path)} if path.exists() else set()
    new_ids = [c["id"] for c in cards if c["id"] not in old_ids]
    print(f"  {len(cards)} cards, {len(new_ids)} new")
    if not dry_run:
        path.write_bytes(data)
    version_path = ROOT / "CardsJson" / "cardsVersion.json"
    version = read_json(version_path)
    version["cardsVersion"] += 1
    write_json(version_path, version, dry_run)
    return cards


def detect_arena_sets(cards):
    stats = json.loads(fetch(FIRE_GLOBAL_URL))
    picked = {s["cardId"] for s in stats["stats"]}
    collectible = {}
    for c in cards:
        if c.get("collectible") and c.get("set"):
            collectible.setdefault(c["set"], set()).add(c["id"])
    coverage = {s: len(ids & picked) / len(ids) for s, ids in collectible.items()}
    sets = [s for s, cov in coverage.items() if cov >= ARENA_SET_MIN_COVERAGE]
    print(f"  Firestone stats from {stats.get('lastUpdated')}: "
          + ", ".join(f"{s} {coverage[s]:.0%}" for s in sets))
    return sets


def update_arena_version(cards, forced_sets, dry_run):
    print("arenaVersion.json")
    path = ROOT / "Arena" / "arenaVersion.json"
    arena = read_json(path)
    sets = forced_sets or detect_arena_sets(cards)
    if not sets:
        sys.exit("  no arena sets detected, refusing to write an empty pool")

    # Keep the current order for sets that stay, append new ones (the app doesn't care, diffs stay small).
    ordered = [s for s in arena["arenaSets"] if s in sets] + sorted(s for s in sets if s not in arena["arenaSets"])
    if ordered == arena["arenaSets"]:
        print("  up to date")
        return ordered
    print(f"  {arena['arenaSets']} -> {ordered}")
    arena["arenaSets"] = ordered
    arena["arenaVersion"] += 1
    write_json(path, arena, dry_run)
    return ordered


def update_hearth_arena(dry_run):
    print("hearthArena.json")
    html = fetch(HEARTHARENA_TIERLIST_URL).decode("utf-8", errors="replace")
    sections = list(re.finditer(r'<section class="tab tierlist [^"]*" id="([a-z-]+)">', html))
    # Each card: its render URL carries the card id, followed by the name and the score
    card_re = re.compile(r'data-card-image="[^"]*/renders/[a-zA-Z]+/([A-Za-z0-9_]+)\.(?:webp|png)">[^<]*</dt>'
                         r'<dd class="score[^"]*">(-?\d+)')
    tierlist = {}
    for i, section in enumerate(sections):
        end = sections[i + 1].start() if i + 1 < len(sections) else len(html)
        # Class sections hold the class cards plus neutrals (scored per class); "any" holds the neutrals
        key = "Neutral" if section.group(1) == "any" else section.group(1).replace("-", " ").title()
        tierlist[key] = {code: int(score) for code, score in card_re.findall(html[section.end():end])}
    if len(tierlist) != 12 or min(len(v) for v in tierlist.values()) < 100:
        sys.exit(f"  unexpected tier list page layout: { {k: len(v) for k, v in tierlist.items()} }")

    path = ROOT / "HearthArena" / "hearthArena.json"
    if path.exists() and read_json(path) == tierlist:
        print("  up to date")
        return
    print("  " + ", ".join(f"{k} {len(v)}" for k, v in tierlist.items()))
    print(f"  write {path.relative_to(ROOT)}")
    if not dry_run:
        path.write_text(json.dumps(tierlist, separators=(",", ":")), encoding="utf-8")
    version_path = ROOT / "HearthArena" / "haVersion.json"
    version = read_json(version_path)
    version["haVersion"] += 1
    write_json(version_path, version, dry_run)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dry-run", action="store_true", help="show what would change without writing files")
    parser.add_argument("--sets", help="comma-separated arena sets, skips Firestone detection")
    args = parser.parse_args()

    cards = update_cards_json(args.dry_run)
    update_arena_version(cards, args.sets.split(",") if args.sets else None, args.dry_run)
    update_hearth_arena(args.dry_run)
    return 0


if __name__ == "__main__":
    sys.exit(main())
