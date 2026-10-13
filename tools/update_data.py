#!/usr/bin/env python3
"""Refresh the data files that Arena Dude downloads from this repo.

- CardsJson/cards.json        <- HearthstoneJSON, trimmed to the fields the app reads and the English names
                                 (bumps cardsVersion.json when it changes)
- Arena/arenaVersion.json     <- arena sets: the sets the HearthArena tier list covers, cross-checked with
                                 Firestone arena card stats (bumps arenaVersion when the set list changes)
- HearthArena/hearthArena.json <- heartharena.com tier list scores per class, also under the pool's reprint codes
                                 (CORE_...) when HearthArena scores the original card (bumps haVersion.json)

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

# A set is in the arena pool when HearthArena scores at least this fraction of its collectible cards (matched by
# name: HearthArena and Firestone keep most Core cards under their original codes). Pool sets sit near 100% (the
# small Event set around 80%); cards from other sets the tier list keeps (discover pools, the last season's leftovers)
# reach about 45%. HearthArena rather than Firestone: Firestone's last-patch stats still mix the old season with the
# new one for days after a patch, while the tier list follows the season from its first day.
ARENA_SET_MIN_COVERAGE = 0.6
# Firestone's stats only cross-check the sets (a disagreement is a WARNING in the log and the data update PR)
FIRE_SET_MIN_COVERAGE = 0.5
# Every pool set must have at least this fraction of its cards scored, or the tier list page has changed
HA_MIN_SET_SCORED = 0.7

# The only fields of cards.json the app reads (Utility::getCardAttribute and the json map). The full file, with
# every card text in 14 languages, is 75 MB: a first run spent minutes on it before it knew any card.
CARD_FIELDS = ("id", "name", "cardClass", "classes", "collectible", "cost", "rarity", "set", "type")


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


def trim_card(card):
    trimmed = {k: card[k] for k in CARD_FIELDS if k in card}
    if isinstance(trimmed.get("name"), dict):
        trimmed["name"] = {"enUS": trimmed["name"].get("enUS", "")}
    return trimmed


def update_cards_json(dry_run):
    print("cards.json")
    cards = [trim_card(c) for c in json.loads(fetch(HSJ_CARDS_URL))]
    data = json.dumps(cards, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
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


def pool_codes_by_set(cards):
    """The collectible cards of each set as the app lists them for a draft (Utility::getSetCodes, no heroes)."""
    by_set = {}
    for c in cards:
        if c.get("collectible") and c.get("set") and not c["id"].startswith(("HERO_0", "HERO_1")):
            by_set.setdefault(c["set"], set()).add(c["id"])
    return by_set


def name_coverage(cards, codes):
    """Per set, the fraction of its collectible card names that one of the codes carries."""
    names = {c["id"]: c.get("name", {}).get("enUS", "") for c in cards}
    covered = {names[code] for code in codes if code in names}
    return {s: len({names[i] for i in ids} & covered) / len({names[i] for i in ids})
            for s, ids in pool_codes_by_set(cards).items()}


def detect_arena_sets(cards, tierlist):
    coverage = name_coverage(cards, {code for scores in tierlist.values() for code in scores})
    sets = [s for s, cov in coverage.items() if cov >= ARENA_SET_MIN_COVERAGE]
    print("  HearthArena covers " + ", ".join(f"{s} {coverage[s]:.0%}" for s in sets))

    try:
        stats = json.loads(fetch(FIRE_GLOBAL_URL))
    except Exception as e:
        print(f"WARNING: Firestone stats not downloaded ({e}), the arena sets are not cross-checked")
        return sets
    fire = name_coverage(cards, {st["cardId"] for st in stats["stats"]})
    fire_sets = {s for s, cov in fire.items() if cov >= FIRE_SET_MIN_COVERAGE}
    for s in sorted(fire_sets.symmetric_difference(sets)):
        print(f"WARNING: {s} is {'in' if s in sets else 'not in'} the pool by HearthArena ({coverage.get(s, 0):.0%}) "
              f"but Firestone stats from {stats.get('lastUpdated')} cover {fire.get(s, 0):.0%} of it")
    return sets


def update_arena_version(cards, tierlist, forced_sets, dry_run):
    print("arenaVersion.json")
    path = ROOT / "Arena" / "arenaVersion.json"
    arena = read_json(path)
    sets = forced_sets or detect_arena_sets(cards, tierlist)
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


def fetch_hearth_arena():
    print("hearthArena.json")
    html = fetch(HEARTHARENA_TIERLIST_URL).decode("utf-8", errors="replace")
    sections = list(re.finditer(r'<section class="tab tierlist [^"]*" id="([a-z-]+)">', html))
    # Each card: its render URL carries the card id, followed by the name (new cards add a <span class="new">)
    # and the score
    card_re = re.compile(r'data-card-image="[^"]*/renders/[a-zA-Z]+/([A-Za-z0-9_]+)\.(?:webp|png)">[^<]*'
                         r'(?:<span[^>]*>[^<]*</span>\s*)*</dt>'
                         r'<dd class="score[^"]*">(-?\d+)')
    tierlist = {}
    for i, section in enumerate(sections):
        end = sections[i + 1].start() if i + 1 < len(sections) else len(html)
        body = html[section.end():end]
        # Class sections hold the class cards plus neutrals (scored per class); "any" holds the neutrals
        key = "Neutral" if section.group(1) == "any" else section.group(1).replace("-", " ").title()
        tierlist[key] = {code: int(score) for code, score in card_re.findall(body)}
        # The tier list entries only: the last section runs on into the page's other card tables
        listed = len(re.findall(r'<dt [^>]*data-card-image="[^"]*/renders/', body))
        if listed != len(tierlist[key]):
            sys.exit(f"  {key}: {listed} cards on the page but {len(tierlist[key])} scores read, the layout changed")
    if len(tierlist) != 12 or min(len(v) for v in tierlist.values()) < 100:
        sys.exit(f"  unexpected tier list page layout: { {k: len(v) for k, v in tierlist.items()} }")
    return tierlist


def add_pool_codes(tierlist, cards, sets):
    """Scores the pool cards HearthArena lists under another code of the same card (the Core reprints CORE_EX1_...
    under EX1_...) under the pool's code too: the app only looks a draft card up by its own code."""
    names = {c["id"]: c.get("name", {}).get("enUS", "") for c in cards}
    pool = set().union(*(ids for s, ids in pool_codes_by_set(cards).items() if s in sets))
    added = 0
    for scores in tierlist.values():
        by_name = {}
        for code in scores:
            by_name.setdefault(names.get(code), []).append(code)
        for code in sorted(pool - scores.keys()):
            alts = by_name.get(names[code], [])
            if code.startswith("CORE_") and code[5:] in alts:
                alts = [code[5:]]
            if alts:
                scores[code] = scores[sorted(alts)[0]]
                added += 1
    print(f"  {added} scores added under the pool's reprint codes")

    scored = {code for scores in tierlist.values() for code in scores}
    for s in sets:
        ids = pool_codes_by_set(cards)[s]
        fraction = len(ids & scored) / len(ids)
        if fraction < HA_MIN_SET_SCORED:
            sys.exit(f"  only {fraction:.0%} of {s} has a HearthArena score, refusing to write the tier list")
        if fraction < 0.9:
            print(f"WARNING: only {fraction:.0%} of {s} has a HearthArena score")


def update_hearth_arena(tierlist, dry_run):
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
    parser.add_argument("--sets", help="comma-separated arena sets, skips the detection")
    args = parser.parse_args()

    cards = update_cards_json(args.dry_run)
    tierlist = fetch_hearth_arena()
    sets = update_arena_version(cards, tierlist, args.sets.split(",") if args.sets else None, args.dry_run)
    add_pool_codes(tierlist, cards, sets)
    update_hearth_arena(tierlist, args.dry_run)
    return 0


if __name__ == "__main__":
    sys.exit(main())
