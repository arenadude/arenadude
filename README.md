# Arena Dude

A draft coach for Hearthstone Arena on macOS. A pixel mascot sits next to the game while you draft, rates every card on offer and tells you which one to take (and what it thinks of your last pick).

- **Card ratings on the draft screen:** a plate under each card with its HearthArena tier score and its Firestone winrate.
- **Advice from the mascot:** it combines both sources into one pick and comments on the draft, your runs and your games.
- **Hero choice:** class winrates on the hero screen.
- **Redraft:** after a loss, which cards to cut from the deck.
- **Run record:** your wins and losses per run.

Arena Dude reads Hearthstone's log files and looks at the screen. It never touches the game client or its memory.

## Requirements

- A Mac with Apple Silicon and a recent macOS (tested on macOS 15).
- Hearthstone in English: the card names are read in English.
- The Screen Recording permission: System Settings → Privacy & Security → Screen & System Audio Recording → add Arena Dude. Without it the app can't see the draft.

## Download

Get the latest build on the [Releases page](https://github.com/arenadude/arenadude/releases/latest), install steps with screenshots on [arenadude.github.io/arenadude](https://arenadude.github.io/arenadude/).

## Building from source

Dependencies (Homebrew): `qt` (Qt 6), `opencv` (OpenCV 5), `libzip`, `pkg-config`.

```sh
brew install qt opencv libzip pkg-config
qmake ArenaDude.pro && make
```

`tools/package_mac.py` turns the built `ArenaDude.app` into a self-contained app that runs on a Mac without Homebrew.

## Data sources

- Card data and images: [HearthstoneJSON](https://hearthstonejson.com) and [Hearthpwn](https://www.hearthpwn.com).
- Tier list: [HearthArena](https://www.heartharena.com).
- Card and class winrates: [Firestone](https://www.firestoneapp.com).

`tools/update_data.py` refreshes the cards, the arena card sets and the tier list in this repo twice a month. The app downloads them from here.

## Credits and license

Arena Dude is based on [Arena Tracker](https://github.com/supertriodo/Arena-Tracker) by Triodo, a HearthSim project, licensed under the GNU GPL v2. Arena Dude is a modified version: it was ported to Qt 6, OpenCV 5 and macOS, its card recognition and draft advice were reworked, and it got a new interface with the mascot. Arena Dude is released under the same license, the [GNU GPL v2](LICENSE).

- `Sources/Utils/libzippp.*`: libzippp by Cédric Tabin, BSD license (see the file headers).
- Mascot font: [Jersey 10](https://fonts.google.com/specimen/Jersey+10), SIL Open Font License (`Fonts/Jersey10-OFL.txt`).

Arena Dude is not affiliated with or endorsed by Blizzard Entertainment. Hearthstone is a trademark of Blizzard Entertainment, Inc.
