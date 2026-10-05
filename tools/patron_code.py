#!/usr/bin/env python3
"""Manage the patron codes that unlock the animated mascot.

The codes themselves are never stored: Version/patronCodes.json keeps their salted SHA-256, the app hashes
what the patron types the same way (MainWindow::patronCodeHash). Spaces, dashes and the case don't count.

Usage:  python3 tools/patron_code.py add [CODE]      add a code (a random one if none given) and print it
        python3 tools/patron_code.py remove CODE     take a code off: the apps using it turn the animation off
        python3 tools/patron_code.py list            how many codes are valid

Then commit and push Version/patronCodes.json (it goes live on push) and post the code for the patrons.
"""

import argparse
import hashlib
import json
import secrets
import sys
from pathlib import Path

SALT = "arenadude-patron:"      # Same as PATRON_CODE_SALT in Sources/mainwindow.h
CODES_FILE = Path(__file__).resolve().parent.parent / "Version" / "patronCodes.json"
ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"     # No 0/O, 1/I: codes are typed by hand


def normalize(code):
    return "".join(c for c in code.upper() if c.isalnum())


def code_hash(code):
    return hashlib.sha256((SALT + normalize(code)).encode()).hexdigest()


def load():
    if CODES_FILE.exists():
        return json.loads(CODES_FILE.read_text())
    return {"codes": []}


def save(data):
    CODES_FILE.write_text(json.dumps(data, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("action", choices=["add", "remove", "list"])
    parser.add_argument("code", nargs="?")
    args = parser.parse_args()
    data = load()

    if args.action == "list":
        print(f"{len(data['codes'])} valid code(s) in {CODES_FILE}")
    elif args.action == "add":
        code = args.code or "DUDE-" + "".join(secrets.choice(ALPHABET) for _ in range(6))
        h = code_hash(code)
        if h in data["codes"]:
            sys.exit(f"{code} is already valid")
        data["codes"].append(h)
        save(data)
        print(f"Added: {code}")
    else:
        if not args.code:
            sys.exit("remove needs the code")
        h = code_hash(args.code)
        if h not in data["codes"]:
            sys.exit(f"{args.code} is not in the list")
        data["codes"].remove(h)
        save(data)
        print(f"Removed: {args.code}")


if __name__ == "__main__":
    main()
