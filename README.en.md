# Desktop Lyrics for Plasma 6

[中文](README.md)

A native Plasma 6 widget that shows synchronized lyrics for the current MPRIS
player. The first release targets NetEase Cloud Music in a browser through
`plasma-browser-integration`, while keeping the provider and frontend seams
open for later additions. A persistent track-info row above the lyrics shows
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
`BUILD_IMPORT_WAYLYRICS`, and `ENABLE_PROVIDER_NETEASE` can disable individual
parts.

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
journalctl --user -u plasma-lyricsd.service -f
```

Per-song timing can be adjusted by 500 ms from the widget context menu. Manual
LRC replacements belong in
`~/.local/share/plasma-lyrics/overrides/<provider>:<track-id>.lrc`.

To import an existing waylyrics JSON cache:

```sh
plasma-lyrics-import-waylyrics --source ~/.cache/waylyrics
```

## Development checks

```sh
ctest --test-dir build --output-on-failure
/usr/lib/qt6/bin/qmllint -I build/bin frontend/plasmoid/package/contents/ui/*.qml
xmllint --noout frontend/plasmoid/package/contents/config/main.xml
QML2_IMPORT_PATH="$PWD/build/bin" plasmoidviewer -a io.github.swim233.plasma-lyrics -f planar
```

License: GPL-2.0-or-later.
