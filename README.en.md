# Desktop Lyrics for Plasma 6

[中文](README.md)

A native Plasma 6 widget that shows synchronized lyrics for the current MPRIS
player. Browser playback through `plasma-browser-integration` and local MPRIS
players are supported. Lyrics use NetEase first and fall back to AMLL TTML DB
by default; the global order and per-song preference are configurable. A persistent track-info row above the lyrics shows
the current title and artist, on by default on the desktop and off in the
panel; both are independently configurable.

## Build

Requirements: CMake 3.24+, Qt 6, KDE Frameworks 6 (ECM and KI18n), Plasma 6,
SQLite's Qt driver, and a C++20 compiler.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
DESTDIR="$PWD/staging" cmake --install build
```

The top-level options `BUILD_DAEMON`, `BUILD_PLASMOID`,
`BUILD_IMPORT_WAYLYRICS`, `ENABLE_PROVIDER_NETEASE`, and
`ENABLE_PROVIDER_AMLL` can disable individual parts.

After installing the package, start the user service:

```sh
systemctl --user enable --now plasma-lyricsd.service
```

Then add “Desktop Lyrics” to either the desktop or a panel. A panel configured
as “Windows Go Below” is Plasma's native way to keep lyrics visible around
maximized windows; desktop widgets cannot be above normal windows.

## Diagnostics

```sh
plasma-lyricsd --explain "Song title" "Artist"
plasma-lyricsd --explain --provider amll --platform apple "Song title" "Artist"
journalctl --user -u plasma-lyricsd.service -f
```

The explanation reports the configured provider chain, provider-specific version
tier, and rejection reasons. An unavailable explicit provider lists the providers
compiled and configured in the current build.

Timing can be adjusted by 500 ms from the widget context menu, per song by
default; the "Global settings" configuration tab can switch this to one
shared offset for every song instead. Manual LRC replacements belong in
`~/.local/share/plasma-lyrics/overrides/<provider>:<track-id>.lrc`.

The same context menu can prefer NetEase or AMLL for the current song, restore
automatic ordering, or force a fresh search. A temporary fallback never
overwrites the saved per-song preference. AMLL word timing and provenance are
preserved in storage and snapshots, while the current UI intentionally remains
line-based rather than showing word-level highlighting.

AMLL TTML DB is a CC0 community database; rights in lyrics and other third-party
works remain with their respective owners. See the
[AMLL TTML DB project](https://github.com/amll-dev/amll-ttml-db) and its contributors.

To import an existing waylyrics JSON cache:

```sh
plasma-lyrics-import-waylyrics --source ~/.cache/waylyrics
```

## Development checks

```sh
ctest --test-dir build --output-on-failure
/usr/lib/qt6/bin/qmllint --bare -I build/bin -I /usr/lib/qt6/qml \
  frontend/plasmoid/package/contents/ui/*.qml \
  frontend/plasmoid/package/contents/ui/config/*.qml
xmllint --noout frontend/plasmoid/package/contents/config/main.xml
QML2_IMPORT_PATH="$PWD/build/bin" plasmoidviewer -a io.github.swim233.plasma-lyrics -f planar
```

Per-release changes are listed in [CHANGELOG.md](CHANGELOG.md).

License: GPL-2.0.
