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

Then: reproduce it on the fixture's replay (`tools/replay_session.py`, with `--screen` if it has a recording), fix, show the fix on the same replay, and run the build check below. The fixture stays in the set, so every later check covers the bug.

## Checking a build

Before a build goes to the user (a dev build to play) or to a release, check it against all the fixtures:

```sh
python3 tools/check_build.py --out <scratchpad>/check-<sha> --baseline <the last good check's --out>
```

- It builds the working tree (or `--app`), replays every fixture session and compares each run with the baseline's; `report.txt` has the differences. Add `--asan` when the change touches threads, memory or containers shared with worker threads. About an hour: the screen sessions run at real time.
- Exit 0, or every difference explained as expected from the change, before handing the build over. Say in the reply what was checked and what wasn't.
- No baseline yet (or it's gone with the scratchpad): run the check on the last good build first (`--app`, e.g. the previous commit's build) and keep that `--out`.
- What the replays can't see, and needs the user's play session: other screen formats (only a 16:10 fullscreen MacBook is recorded), windowed Hearthstone, macOS Game Mode, dialogs, Hearthstone screens no fixture has.
- Dev builds for the user: `qmake ... DEFINES+=AT_DEV_BUILD` (keeps the unread screens), copied to a uniquely named, ad-hoc signed .app (macOS ties the Screen Recording grant to it).

Screen recordings make the screen half replayable. When the user plays with the screen recorded (Cmd+Shift+5), cut the draft and redraft moments into `<fixture>/screen/` as clips (`ffmpeg -ss <s> -t <s> -i <mov> -vf fps=10 -c:v libx264 -crf 14 -pix_fmt yuv420p`) with a `screen.json`: the clips' start times (the recording's start is in its file name), the screen size and pixel ratio, Hearthstone's window rect in points, and `"hide": ["mascotBubble"]` when the tracker was on screen (see the promo fixture and `tools/replay_session.py`). Screen replays need the release build (ASan is too slow for real time) and `--speed 5` at most between the clips.

## Project

Arena Dude (formerly the Arena Tracker fork, AT; the code still says AT in places) is a Qt 6 / C++ draft coach for Hearthstone Arena with a pixel mascot. **macOS only**: the Windows and Linux code was deleted (a port would be written again). Single qmake project, no unit tests (`tools/replay_session.py` replays recorded logs, and with `--screen` a fixture's screen recordings, see `Sources/Utils/replayscreen.h`; `tools/compare_replays.py` compares two runs or a run with the live log, `tools/check_build.py` runs them all, see Checking a build), no linter. Code comments are frequently in Spanish.

## Build

Dependencies (Homebrew): Qt 6 (modules `core gui network widgets`), OpenCV 5 (pkg-config `opencv5`, only the modules listed in `ArenaDude.pro`), libzip, zlib.

```sh
qmake ArenaDude.pro && make        # or open ArenaDude.pro in Qt Creator, Release build
```

- Every new `.cpp`/`.h` must be added to `SOURCES`/`HEADERS` in `ArenaDude.pro`.
- Resources (images, fonts) are bundled through `arenadude.qrc`. There is no `.ui` file: the windows are built in code.
- App version is `VERSION` in `Sources/versionchecker.h`.

Release: bump `VERSION`, build, `tools/package_mac.py <build>/ArenaDude.app "<out>/Arena Dude.app"` (self-contained, named and versioned, ad-hoc signed), `ditto -c -k --keepParent "Arena Dude.app" Arena.Dude.Mac.zip` (always this name: the landing page and the release notes link `releases/latest/download/Arena.Dude.Mac.zip`), publish a GitHub release `vX.Y.Z` with the zip, and only then append `vX.Y.Z` to `versionFree` in `Version/version.json` (it goes live on push: listing a version before its release sends users to a missing page). The app is not notarized: the release notes explain how to open it the first time (System Settings → Privacy & Security → Open Anyway) and grant Screen Recording.

Debug toggles (compile-time) are the `DEBUG_*` defines in `Sources/utility.h`. Test builds handed to the user get `qmake ... DEFINES+=AT_DEV_BUILD`: they keep the screens the OCR couldn't read in `~/Arena Dude/Unread` (a release only logs the text read, the screen can show the player's other apps).

## Architecture

**Wiring hub:** `MainWindow` (`Sources/mainwindow.cpp`) is never shown. It creates every handler in `create*Handler()` methods, connects them with old-style `SIGNAL()/SLOT()` string connections, downloads the data files, and holds the mascot's reactions (`mascot*`). Handlers are mostly decoupled from each other; cross-handler data flow goes through signals connected in `MainWindow`. When adding a signal, remember to wire it there (handlers re-emit `pDebug` to MainWindow, which writes the log).

**Game-state pipeline (Hearthstone log parsing):**
1. `LogLoader` finds the HS logs dir and creates one `LogWorker` per component (`LoadingScreen`, `Power`, `Zone`, `Arena`, `Asset`), each tailing its log file.
2. Lines are optionally merged/sorted across components by timestamp (`sortLogs`) and emitted as `newLogLineRead(LogComponent, line, ...)`.
3. `GameWatcher` parses lines with regexes (`processPower`, `processZone`, `processArena`, ...) and emits high-level signals: the arena and draft modes (`newArena`, `redraft`, `pickCard`, `deckSnapshotRead`, `inRewards`, ...) and of a game only `startGame`, `endGame`, `newGameResult`, `enemySecretPlayed`, `enemyHero` (the opponent's hero, from Zone.log) and `mulliganDone` (the match tracking was removed).
4. Handlers consume those signals: `DeckHandler` (the drafted deck as a model, synced with Hearthstone's deck snapshots; `deckCardList[0]` counts the unknown cards), `DraftHandler`, `ArenaHandler` (the run record in `ArenaDudeStats.json`) and the mascot reactions in `MainWindow`.

**Drafting:** `DraftHandler` (largest file) screen-captures the draft, locates card slots via template images in `Extra/*Template*.png`, and identifies cards by OpenCV histogram comparison against downloaded card images. It first reads each card's name banner with Apple Vision (`Sources/Utils/macocr.mm`, `DraftHandler::readCardNames`) and fuzzy-matches it against `cardsNameMap`; a matched name overrides the histogram (needed for animated golden cards). It scores picks from two sources (`DraftMethod` enum: HearthArena tier list and Firestone winrates via `WinratesDownloader`), combined by `PickRating` into the hands on the plates and the mascot's advice.

**UI:** the user sees only `MascotWindow` (the mascot and its speech bubble), the draft overlays (`DraftScoreWindow` with the `ScorePlate`s under the cards, `DraftHeroWindow` on the hero choice) and `SplashWindow`. The old tracker windows are gone; the settings of their config tab that still matter are read from QSettings in `MainWindow::readSettings`. The class winrates of the hero choice are static in `WinratesDownloader` (`getHeroScore`, `getHeroGames`). Only the resources in `arenadude.qrc` are in the binary: the mascot sprites and font, the plate logos and the fallback class icons.

**Mascot:** its lines and reactions are the `mascot*` slots of `MainWindow` (the voice: smug but good-hearted). At an arena game's mulligan it lists the opponent's 10 top cards (`WinratesDownloader::getTopCards`: copies per game × (win rate when drawn − (class win rate − 2 pt)), from the Firestone per-class files). `MainWindow::checkSystemDialog` polls the window list every second: a macOS dialog (UserNotificationCenter, e.g. the "bypass the system private window picker" reminder) over Hearthstone makes the mascot blind and ask for Allow. `MascotWindow` plays a mood's frames (`Images/Mascot/<mood>_0.png`, `_1`... with `frameSteps` timings) when `setAnimated` (the `mascotAnimated` setting, meant for supporters); every sprite is drawn at the Idle sprite's pixel scale and a taller one hangs below the anchor. Sprites are 389 px wide, palette black/white/grey 125/purple (107,27,155)/red, hard alpha.

**Card model:** Card data comes from HearthstoneJSON `cards.json`, trimmed by `tools/update_data.py` to the fields the app reads and the English names (the full file is 75 MB), cached in `Extra/`; it can arrive after Hearthstone's deck or a mulligan on a first run (`DeckHandler::refreshCardData`, `MainWindow::mascotMulliganRetry`); card metadata lookups are static helpers in `Utility`. Card-ID constants (secrets, special cards) are in `Sources/constants.h`. `Sources/Cards/` holds the card data types: `DeckCard` (a deck card, its copies and redraft scores) and `DraftCard` (a draft candidate with its match quality).

**Settings/storage:** `QSettings settings;` with the names set in `main()` (domain `com.arena-dude.Arena Dude`; `AT_SETTINGS_APP` gives a test run its own); user data dir is `~/Arena Dude` (`Utility::migrateFromArenaTracker` moves the old `~/Arena Tracker` dir and settings once).

## Repo data served to running clients

The installed app downloads data directly from this repo's (`arenadude/arenadude`) `master` branch via `raw.githubusercontent.com` (URLs in `mainwindow.h`, `versionchecker.h`). Committing to these directories affects all live users immediately:

- `Version/version.json` — `versionFree` lists the versions allowed to run (the last one is the latest; a version not listed must update or quit), `downloadUrl` the release page the update dialog opens (`vx.x` = latest), `log` the changes shown on the first run of the latest. The app never replaces itself.
- `Version/patronCodes.json` — salted SHA-256 of the codes that unlock the animated mascot (the patrons' thank-you, posted on Patreon). Managed with `tools/patron_code.py add|remove|list`; the app checks a typed code and re-checks the saved one at every start, so removing a code turns its animation off. Fetched on every check, no companion version file.
- `Arena/arenaVersion.json` — current arena card sets, `trustHA`, reset counters.
- `HearthArena/hearthArena.json` + `haVersion.json`, `CardsJson/`, the template and mana/rarity files in `Extra/` (`MainWindow::downloadExtraFiles`) and `Images/icon.png`. The other `Images/` files are only bundled through `arenadude.qrc`.

Clients only re-download a JSON when its companion `*Version.json` number increases — bump the version number whenever the data file changes.

`tools/update_data.py` refreshes cards.json, arena sets and the HearthArena tier list (bumping the versions).

Card images are not in the repo: `HSCardDownloader` fetches the HearthstoneJSON render of a plain card and the first frame of Hearthpwn's golden animation, cuts them to 200x304 and caches them in `~/Arena Dude/Hearthstone Cards`. Hero portraits are not downloaded: the hero choice is read by OCR from the class labels. `.github/workflows/update-data.yml` runs it on the 1st and 15th of each month (or manually from the Actions tab) and opens a "Data update" pull request.


