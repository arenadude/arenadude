# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Language

Communicate with the user in Russian.

Write all new code comments in English (existing Spanish comments can stay as they are).

## Git

**Never push to any remote** (`git push` in any form, including `--force`, tags, or pushing via `gh`). The user pushes personally. Committing locally is fine when asked.

## Bug reports from play sessions

When the user reports a bug they hit while playing (draft, game, mascot, overlay), archive the session **before** investigating: Hearthstone keeps only its last couple of log sessions and the tracker log rotates on every restart.

```sh
python3 tools/collect_session.py -n "<short English description>"
```

Screenshots: if the user attached one to the chat, use that and don't touch the clipboard. An attachment with a file path goes in with `-s <path>`. A pasted image without a path can't be saved, so describe what it shows in `note.md`. Only when nothing is attached and the user says they took a screenshot, add `-c` to save the clipboard image (their screenshots go to the clipboard). Then fill the `Expected` / `Actual` sections of the fixture's `note.md` from what the user said. Fixtures live in `~/Desktop/hs fixtures`, outside the repo: they contain BattleTags, never commit them.

## Project

Arena Dude (formerly the Arena Tracker fork, AT; the code still says AT in places) is a Qt 6 / C++ draft coach for Hearthstone Arena with a pixel mascot. **macOS only**: the Windows and Linux code was deleted (a port would be written again). Single qmake project, no unit tests (`tools/replay_session.py` replays recorded logs), no linter. Code comments are frequently in Spanish.

## Build

Dependencies (Homebrew): Qt 6 (modules `core gui network widgets`), OpenCV 5 (pkg-config `opencv5`, only the modules listed in `ArenaDude.pro`), libzip, zlib.

```sh
qmake ArenaDude.pro && make        # or open ArenaDude.pro in Qt Creator, Release build
```

- Every new `.cpp`/`.h` must be added to `SOURCES`/`HEADERS` in `ArenaDude.pro`.
- Resources (images, fonts) are bundled through `arenadude.qrc`. There is no `.ui` file: the windows are built in code.
- App version is `VERSION` in `Sources/versionchecker.h`.

Release: bump `VERSION`, build, `tools/package_mac.py <build>/ArenaDude.app "<out>/Arena Dude.app"` (self-contained, named and versioned, ad-hoc signed), `ditto -c -k --keepParent "Arena Dude.app" Arena.Dude.vX.Y.Z.Mac.zip`, publish a GitHub release `vX.Y.Z` with the zip, and only then append `vX.Y.Z` to `versionFree` in `Version/version.json` (it goes live on push: listing a version before its release sends users to a missing page). The app is not notarized: the release notes explain how to open it the first time (System Settings → Privacy & Security → Open Anyway) and grant Screen Recording.

Debug toggles (compile-time) are the `DEBUG_*` defines in `Sources/utility.h`.

## Architecture

**Wiring hub:** `MainWindow` (`Sources/mainwindow.cpp`) is never shown. It creates every handler in `create*Handler()` methods, connects them with old-style `SIGNAL()/SLOT()` string connections, downloads the data files, and holds the mascot's reactions (`mascot*`). Handlers are mostly decoupled from each other; cross-handler data flow goes through signals connected in `MainWindow`. When adding a signal, remember to wire it there (handlers re-emit `pDebug` to MainWindow, which writes the log).

**Game-state pipeline (Hearthstone log parsing):**
1. `LogLoader` finds the HS logs dir and creates one `LogWorker` per component (`LoadingScreen`, `Power`, `Zone`, `Arena`, `Asset`), each tailing its log file.
2. Lines are optionally merged/sorted across components by timestamp (`sortLogs`) and emitted as `newLogLineRead(LogComponent, line, ...)`.
3. `GameWatcher` parses lines with regexes (`processPower`, `processZone`, `processArena`, ...) and emits high-level signals: the arena and draft modes (`newArena`, `redraft`, `pickCard`, `deckSnapshotRead`, `inRewards`, ...) and of a game only `startGame`, `endGame`, `newGameResult` and `enemySecretPlayed` (the match tracking was removed).
4. Handlers consume those signals: `DeckHandler` (the drafted deck as a model, synced with Hearthstone's deck snapshots; `deckCardList[0]` counts the unknown cards), `DraftHandler`, `ArenaHandler` (the run record in `ArenaDudeStats.json`) and the mascot reactions in `MainWindow`.

**Drafting:** `DraftHandler` (largest file) screen-captures the draft, locates card slots via template images in `Extra/*Template*.png`, and identifies cards by OpenCV histogram comparison against downloaded card images. It first reads each card's name banner with Apple Vision (`Sources/Utils/macocr.mm`, `DraftHandler::readCardNames`) and fuzzy-matches it against `cardsNameMap`; a matched name overrides the histogram (needed for animated golden cards). It scores picks from two sources (`DraftMethod` enum: HearthArena tier list and Firestone winrates via `WinratesDownloader`), combined by `PickRating` into the hands on the plates and the mascot's advice.

**UI:** the user sees only `MascotWindow` (the mascot and its speech bubble), the draft overlays (`DraftScoreWindow` with the `ScorePlate`s under the cards, `DraftHeroWindow` on the hero choice) and `SplashWindow`. The old tracker windows are gone; the settings of their config tab that still matter are read from QSettings in `MainWindow::readSettings`. The class winrates of the hero choice are static in `WinratesDownloader` (`getHeroScore`, `getHeroGames`). Only the resources in `arenadude.qrc` are in the binary: the mascot sprites and font, the plate logos and the fallback class icons.

**Card model:** Card data comes from HearthstoneJSON `cards.json` (cached); card metadata lookups are static helpers in `Utility`. Card-ID constants (secrets, special cards) are in `Sources/constants.h`. `Sources/Cards/` holds the card data types: `DeckCard` (a deck card, its copies and redraft scores) and `DraftCard` (a draft candidate with its match quality).

**Settings/storage:** `QSettings settings;` with the names set in `main()` (domain `com.arena-dude.Arena Dude`; `AT_SETTINGS_APP` gives a test run its own); user data dir is `~/Arena Dude` (`Utility::migrateFromArenaTracker` moves the old `~/Arena Tracker` dir and settings once).

## Repo data served to running clients

The installed app downloads data directly from this repo's (`arenadude/arenadude`) `master` branch via `raw.githubusercontent.com` (URLs in `mainwindow.h`, `versionchecker.h`). Committing to these directories affects all live users immediately:

- `Version/version.json` — `versionFree` lists the versions allowed to run (the last one is the latest; a version not listed must update or quit), `downloadUrl` the release page the update dialog opens (`vx.x` = latest), `log` the changes shown on the first run of the latest. The app never replaces itself.
- `Arena/arenaVersion.json` — current arena card sets, `trustHA`, reset counters.
- `HearthArena/hearthArena.json` + `haVersion.json`, `CardsJson/`, the template and mana/rarity files in `Extra/` (`MainWindow::downloadExtraFiles`) and `Images/icon.png`. The other `Images/` files are only bundled through `arenadude.qrc`.

Clients only re-download a JSON when its companion `*Version.json` number increases — bump the version number whenever the data file changes.

`tools/update_data.py` refreshes cards.json, arena sets and the HearthArena tier list (bumping the versions).

Card images are not in the repo: `HSCardDownloader` fetches the HearthstoneJSON render of a plain card and the first frame of Hearthpwn's golden animation, cuts them to 200x304 and caches them in `~/Arena Dude/Hearthstone Cards`. Hero portraits are not downloaded: the hero choice is read by OCR from the class labels. `.github/workflows/update-data.yml` runs it on the 1st and 15th of each month (or manually from the Actions tab) and opens a "Data update" pull request.


