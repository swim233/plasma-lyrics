# Desktop Lyrics for Plasma 6

[中文](README.md)

A native Plasma 6 widget that shows synchronized lyrics for the current MPRIS
player. Browser playback through `plasma-browser-integration` and local MPRIS
players are supported. Lyrics use local files first, then NetEase and AMLL TTML DB
by default; sources can be reordered, enabled individually, and preferred per song. A persistent track-info row above the lyrics shows
the current title and artist, on by default on the desktop and off in the
panel; both are independently configurable. Each widget keeps a light and a
dark set of appearance settings and follows the system colour scheme, or
stays on one set.

## Build

Requirements: CMake 3.24+, Qt 6, KDE Frameworks 6 (ECM and KI18n), Plasma 6,
SQLite's Qt driver, zlib and fontconfig development files, and a C++20 compiler.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
DESTDIR="$PWD/staging" cmake --install build
```

The top-level options `BUILD_DAEMON`, `BUILD_PLASMOID`, and
`BUILD_IMPORT_WAYLYRICS` can disable individual components. All lyric providers
are always built; enable, disable, and reorder them in the Lyrics Sources settings.
Setting the retired `ENABLE_PROVIDER_NETEASE`, `ENABLE_PROVIDER_AMLL`, or
`ENABLE_PROVIDER_QQ` option to `OFF` now fails configuration. Remove these options
from build arguments and existing CMake caches (clear cached values with
`cmake -S . -B build -U 'ENABLE_PROVIDER_*'`).

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

The "Record debug details" checkbox under the "Lyrics Service" configuration
page's Diagnostics section (`logging/debug`, off by default) turns on debug-level
logging for seven categories, effective after the service restarts:

| Category | Covers |
| --- | --- |
| `plasmalyrics.daemon` | Service lifecycle and control calls |
| `plasmalyrics.resolver` | The lyric resolution pipeline |
| `plasmalyrics.mpris` | MPRIS player discovery and state |
| `plasmalyrics.provider.netease` | NetEase lyric source requests |
| `plasmalyrics.provider.amll` | AMLL TTML database lyric source requests |
| `plasmalyrics.provider.local` | The local lyrics directory |
| `plasmalyrics.provider.qq` | QQ Music lyric source requests |

That checkbox is the way to turn on every category at once; to debug a single
category instead, use the `QT_LOGGING_RULES` environment variable, which takes
precedence over the config value. The service runs under `systemd --user`, so the
variable has to go through `set-environment` for that user instance before
restarting the service:

```sh
systemctl --user set-environment QT_LOGGING_RULES="plasmalyrics.mpris.debug=true"
systemctl --user restart plasma-lyricsd

# to revert
systemctl --user unset-environment QT_LOGGING_RULES
systemctl --user restart plasma-lyricsd
```

With the KDE package installed (`BUILD_PLASMOID=ON`), `kdebugsettings` shows eight
categories — the seven above plus the widget's own `plasmalyrics.config` (see
"Settings change log" below). "Record debug details" only governs the seven
above: those seven can be toggled individually in `kdebugsettings`, but only
while that checkbox is off, since it takes precedence over kdebugsettings and
overrides their per-category debug toggle while it's on. `plasmalyrics.config`
is unaffected by that checkbox; it can only be toggled in `kdebugsettings` or
via `QT_LOGGING_RULES` set in plasmashell's own environment.

The line format depends on where the log goes. Under the systemd journal each line is `category message` — journald records the timestamp and level itself, so `journalctl` colours entries by level and `journalctl --user -u plasma-lyricsd -p 4` shows warnings and above. Run in a terminal the format is `[time] level category message`, coloured by level; redirected to a file or pipe it is the same text without colour. In all three forms, and in the log file, category names drop the `plasmalyrics.` prefix (`plasmalyrics.mpris` from the table above appears as `mpris`), but `QT_LOGGING_RULES` and `kdebugsettings` still need the full name. Every resolve is tagged
with a `#number` prefix shared by all its log lines; gaps in the numbering are
normal — a cancellation also consumes a number whenever the daemon finds nothing
playable to resolve: no player at startup, the last player exiting, or every
remaining source being filtered out and not playing. At info level, a resolve logs
one line each at its start, at the key steps along the way, and at its end:

```
#42 resolve: trigger=track-changed identity="Spotify" service=org.mpris.MediaPlayer2.spotify fingerprint="mediaSrc:0f3e…" platform=netease music=true title="劣等上等" artist="鏡音リン"
#42 search provider=netease candidates=1 selected=1294899572 score=0.900 elapsed=1488ms
#42 state=ok from=provider source=netease/1294899572 lines=59 elapsed=2061ms
```

`trigger=` on the start line explains why the resolve ran: `startup` (the service's
first resolve), `track-changed` (same player, new track), `player-changed` (the
active player changed), `replay` (the same track looped into a new round),
`research` (the widget menu's "Search for lyrics again"), `set-preferred` (a
per-song provider preference was set), or `clear-preferred` (a per-song preference
was cleared).

With "Record debug details" on, this also prints debug-level lines such as
per-candidate match scoring and cache-lookup details.

**Settings change log**: saving either the "Lyrics Service" or the "Global settings"
page, changing any setting on the Desktop appearance / Panel appearance / Text
pages, and "Save and restart the service" and its outcome, each log one line,
for example:

```
config changed store=db key=globalOffsetMs old="0" new="-300"
config changed store=applet applet=12 form=desktop key=desktopFontSize old="34" new="36"
```

The widget's copy lands in plasmashell's own log; read it with
`journalctl --user QT_CATEGORY=plasmalyrics.config`. The daemon receives the same
line and logs it under `plasmalyrics.daemon`, so it also appears in
`journalctl --user -u plasma-lyricsd` and the optional log file — while the service
isn't running, only the widget's copy exists. `network/proxyUrl` is logged without
credentials.

Timing can be adjusted by 500 ms from the widget context menu, per song by
default; the "Global settings" configuration tab can switch this to one
shared offset for every song instead. Manual LRC replacements belong in
`~/.local/share/plasma-lyrics/overrides/<provider>:<track-id>.lrc`.

The searchable local source first checks for an `.lrc` sidecar beside a local
audio file, then recursively scans the configurable lyrics directory (default
`~/.local/share/plasma-lyrics/lyrics/`). Files there are matched from their
names and LRC `[ti:]`, `[ar:]`, `[al:]`, and `[length:]` tags; the parsed index
is reused until the directory contents change. This directory is
separate from the exact provider/track replacement directory above.

When the player reports a title carrying a translated parenthetical (for example
`青さは止んだ (青春已逝)`), matching retries with the parenthetical stripped. A
match that only succeeds that way is accepted only if both durations are known and
within 2 seconds of each other, so it cannot latch onto an unrelated track of the
same name. An `.lrc` in the local lyrics directory without a `[length:mm:ss]` tag
has no duration to compare, so such titles will not match until the tag is added
(a sidecar beside the audio file is unaffected — it inherits the duration the
player reports). The AMLL index carries no duration, so such titles never match
through that source.

Both directories support bilingual `.lrc`: when two adjacent lines share the
same timestamp, the first is the original and the second its translation;
only the first two lines at a shared timestamp are used, any further line is
ignored. A pair where either line looks like a production credit (for example
`Lyricist:`) is left as-is.

The same context menu can prefer local files, NetEase, or AMLL for the current song, restore
automatic ordering, or force a fresh search. A temporary fallback never
overwrites the saved per-song preference. AMLL word timing and provenance are
preserved in storage and snapshots, while the current UI intentionally remains
line-based rather than showing word-level highlighting.

AMLL TTML DB is a CC0 community database; rights in lyrics and other third-party
works remain with their respective owners. See the
[AMLL TTML DB project](https://github.com/amll-dev/amll-ttml-db) and its contributors.

The "Lyrics Service" configuration page also has a network proxy setting: direct
connection, system proxy, or a custom `socks5://host:port` or `http://host:port`
address, optionally with a `user:password` stored in plain text. It applies to
NetEase and AMLL, with no exemption for loopback addresses. An invalid custom
address is rejected on save; one already on disk leaves both sources unavailable
after the next service restart, falling back to local files only.

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
