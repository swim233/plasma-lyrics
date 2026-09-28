<div align="center">

# 🎵 Desktop Lyrics for Plasma 6

**A native Plasma 6 synchronized-lyrics widget — follows any MPRIS player in the session and shows scrolling lyrics on the desktop and in panels, word-by-word lyrics included**

[![Release](https://img.shields.io/github/v/release/swim233/plasma-lyrics?include_prereleases&style=flat-square&logo=github&color=1D99F3)](https://github.com/swim233/plasma-lyrics/releases)
[![CI](https://img.shields.io/github/actions/workflow/status/swim233/plasma-lyrics/ci.yml?branch=main&style=flat-square&logo=githubactions&logoColor=white&label=CI)](https://github.com/swim233/plasma-lyrics/actions/workflows/ci.yml)
[![AUR](https://img.shields.io/aur/version/plasma-lyrics-git?style=flat-square&logo=archlinux&logoColor=white&label=AUR)](https://aur.archlinux.org/packages/plasma-lyrics-git)
[![License](https://img.shields.io/github/license/swim233/plasma-lyrics?style=flat-square&color=blue)](https://github.com/swim233/plasma-lyrics/blob/main/LICENSE)

[![Plasma](https://img.shields.io/badge/KDE_Plasma-6-1D99F3?style=flat-square&logo=kde&logoColor=white)](https://kde.org/plasma-desktop/)
[![Qt](https://img.shields.io/badge/Qt-6.6+-41CD52?style=flat-square&logo=qt&logoColor=white)](https://www.qt.io/)
[![C++](https://img.shields.io/badge/C%2B%2B-20-00599C?style=flat-square&logo=c%2B%2B&logoColor=white)](https://isocpp.org/)

<img width="1672" height="941" alt="image" src="https://github.com/user-attachments/assets/b764b95a-2f49-4f84-abc9-baaea75e92e1" />


[中文](README.md) · [Changelog (Chinese)](CHANGELOG.md) · [Report an issue](https://github.com/swim233/plasma-lyrics/issues)

</div>

---

Watches the MPRIS players in the current session and shows synchronized lyrics.
Through `plasma-browser-integration` it supports the NetEase Cloud Music and
Apple Music web players in a browser; local MPRIS players are supported too,
including the Apple Music clients Cider and Sidra.

By default lyrics come from local lyrics, NetEase, AMLL TTML DB and QQ Music, in
that order, falling back automatically when the preferred source has no usable
lyrics.

A persistent track-info row (title — artist) sits above the lyrics, on by default
on the desktop and off in panels; the two are configured independently.

## ✨ Features

- 🖥️ **Desktop and panel forms** — one widget, two forms, each with its own independent appearance settings
- 🎤 **Synchronized scrolling lyrics** — line-by-line highlighting, an optional secondary line (translation or romanization) with its own font settings
- 🔀 **Multi-source fallback** — local → NetEase → AMLL → QQ Music by default; drag to reorder, enable or disable each source, prefer a source for the current song, or search again right away
- 📁 **Local lyrics source** — reads an `.lrc` beside the local audio file first, and can also scan a lyrics directory of your choice with the same matching rules
- 📚 **AMLL TTML DB and QQ Music** — word-by-word highlighting and animation, with particles that can float up from each word as it is sung
- 🪟 **Visible next to maximized windows** — in a panel set to "Windows go below", the lyrics stay visible beside maximized windows
- 🎨 **Customizable appearance** — background style (Plasma theme / solid translucent color / none), text outline, font (searchable list of installed fonts; the track info can use its own), font size, weight and colors
- 🌗 **Light and dark appearance** — a light and a dark set of appearance settings, switching automatically with a fade as the Plasma style turns light or dark, or pinned to one set
- 📏 **Overflow strategies** — scale to fit `fit` / wrap `wrap` / marquee `marquee`
- 🎞️ **Line transitions** — none / fade / slide
- 💤 **Auto-hide** — hides once playback has been idle longer than a configurable grace period (the desktop widget fades out, the panel widget gives its space back) and comes back when a track starts; off by default
- ⏱️ **Timing adjustment** — shift by 0.5 s either way from the context menu, stored per song and shared by every widget; "Global settings" can add one global offset shared by all songs, added to each song's own offset, and can edit the current song's offset directly
- 📝 **Manual lyric overrides** — drop in an `.lrc` file to replace any song's lyrics

## 📦 Installation

### Arch Linux (AUR, recommended)

The AUR has three packages that conflict with each other; install one of them:

| Package             | Description                                                        |
| ------------------- | ------------------------------------------------------------------ |
| `plasma-lyrics`     | Downloads the latest release's source and builds it locally        |
| `plasma-lyrics-bin` | Installs the prebuilt release from GitHub Releases, no local build |
| `plasma-lyrics-git` | Builds the latest code on the `main` branch locally                |

```sh
yay -S plasma-lyrics      # or paru -S plasma-lyrics; swap in the package name for the other two
```

### Debian 13 (.deb)

Download the `.deb` and `SHA256SUMS` from
[GitHub Releases](https://github.com/swim233/plasma-lyrics/releases), verify the
checksum, then install:

```sh
sudo apt install ./plasma-lyrics_*_amd64.deb
```

Each release also ships an Arch `.pkg.tar.zst` and a source tarball.

### Build from source

Building needs CMake 3.24+, Ninja, a C++20 compiler, Qt 6 (6.6 or later,
including Qt Declarative), KDE Frameworks 6 (ECM and KI18n), Plasma 6
(libplasma), gettext (builds the translations), and the zlib and fontconfig
development files. At runtime Qt's SQLite driver is also needed (the lyrics cache
uses it), and the widget additionally needs Kirigami, KSvg and KDeclarative (the
settings pages' color buttons come from its `org.kde.kquickcontrols`).

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build
```

The top-level CMake options `BUILD_DAEMON`, `BUILD_PLASMOID` and
`BUILD_IMPORT_WAYLYRICS` switch off individual components. All lyric sources are
always built; enable, disable and reorder them on the widget's "Lyrics Service"
settings page. Setting the retired `ENABLE_PROVIDER_NETEASE`,
`ENABLE_PROVIDER_AMLL` or `ENABLE_PROVIDER_QQ` option to `OFF` fails
configuration; remove these options from build arguments and existing CMake caches
(clear cached values with `cmake -S . -B build -U 'ENABLE_PROVIDER_*'`).

## 🚀 Usage

### 1. Start the daemon

After installing, enable the user service:

```sh
systemctl --user enable --now plasma-lyricsd.service
```

### 2. Add the widget

Right-click the desktop or a panel → "Add or Manage Widgets…" → find
**"Desktop Lyrics"** and drag it in.

> [!TIP]
> **Visible next to maximized windows**: setting the "Visibility" of the panel
> that holds the lyrics to "Windows go below" is Plasma's native way to keep
> lyrics visible around maximized windows; desktop widgets cannot sit above
> normal windows.

### 3. Play music

Open any MPRIS player (such as the NetEase Cloud Music web player in a browser)
and the lyrics appear automatically.

### Common tasks

| Task                             | How                                                                                                                                            |
| -------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------- |
| Lyrics too early / too late      | Right-click the widget → shift this song's offset by **±0.5 s**; "Global settings" can add a global offset or edit this song's offset directly |
| Choose the current song's source | Right-click the widget → automatic, or prefer local files, NetEase, AMLL or QQ Music; a temporary fallback never overwrites that choice        |
| Search again right away          | Right-click the widget → "Search for lyrics again", bypassing existing matches and the negative cache                                          |
| Change the appearance            | Right-click the widget → "Configure Desktop Lyrics..."; the desktop and panel forms are configured independently                               |
| Replace a song's lyrics          | Put an `.lrc` at `~/.local/share/plasma-lyrics/overrides/<provider>:<track-id>.lrc` (bilingual supported, see "Lyric sources" below)           |
| Import a waylyrics cache         | `plasma-lyrics-import-waylyrics --source ~/.cache/waylyrics`                                                                                   |

## 🎵 Supported players

A "player" here is whatever reports MPRIS playback state, not the provider the
lyrics come from.

| Player                         | Notes                                                                                                         |
| ------------------------------ | ------------------------------------------------------------------------------------------------------------- |
| NetEase Cloud Music web player | Connected through `plasma-browser-integration` (a browser extension); the primary source in the first release |
| Apple Music web player         | Also connected through `plasma-browser-integration` (`music.apple.com`)                                       |
| Local MPRIS players            | Any player that implements the MPRIS interface; Cider and Sidra are treated as Apple Music                    |
| KDE Connect phones             | Supported but ignored by default (adjustable under "Player blacklist" on the "Lyrics Service" page)           |

The NetEase Cloud Music and Apple Music platforms can each be switched off under
"Enable lyrics for these platforms" on the "Lyrics Service" page.

## 🎤 Lyric sources

| Source       | Notes |
| ------------ | ----- |
| Local files  | First by default; checks for an `.lrc` beside the local audio file, then recursively scans the configured lyrics directory, matching title, artist, album and duration from file names and the LRC `[ti:]`, `[ar:]`, `[al:]` and `[length:]` tags |
| NetEase      | Online search for LRC and translations |
| AMLL TTML DB | Default fallback source; searches a cached metadata index locally and downloads TTML on demand; provides translations and word-by-word lyrics |
| QQ Music     | Last by default; online search, provides word-by-word lyrics, translations and romanization (currently the only source with romanization) |

The widget's "Lyrics Service" settings page lets you drag to set the global
order, tick the sources to enable, choose the local lyrics directory, and adjust
NetEase's and AMLL's network addresses and timeouts and the AMLL index refresh
interval. The `.lrc` files in the lyrics directory and its subdirectories get a
reusable search index that is only parsed again once the directory contents
change; this is separate from the override directory below, which replaces an
existing result by exact provider and track id.

When the player reports a title carrying a Chinese translated parenthetical (for
example `青さは止んだ (青春已逝)`), matching also retries with the parenthetical
stripped. A result that only matches that way is accepted only if the two
durations are within 2 seconds of each other, so it cannot latch onto an
unrelated track of the same name. An `.lrc` in the local lyrics directory without
a `[length:mm:ss]` tag has no duration to compare, so such titles will not match
until the tag is added (a sidecar beside the audio file is unaffected — it takes
the duration the player reports). The AMLL index carries no duration, so such
titles never match through that source.

Local lyrics (the `.lrc` beside the audio file and the lyrics directory) and the
override directory both support bilingual `.lrc`: when a timestamp has several
lines, the first is the original and the second its translation, and any further
line is ignored; if either of the first two lines looks like a production credit
(for example `作词：` or `Lyricist:`), every line at that timestamp is kept as-is.

AMLL TTML DB is provided under CC0; rights in the original lyrics and other
third-party content remain with their respective owners. See
[AMLL TTML DB](https://github.com/amll-dev/amll-ttml-db) for the project and its
contributors.

The "Lyrics Service" settings page also has a network proxy setting: direct
connection, system proxy, or a custom address (`socks5://host:port` or
`http://host:port`, optionally with a user name and password, stored in plain
text). It applies to all three network sources — NetEase, AMLL and QQ Music —
with no exemption for loopback addresses. An invalid address cannot be saved from
the settings page; an invalid address already saved leaves those three sources
unavailable after the service restarts, leaving only the local files source.

## ⚙️ Configuration and data files

| Path                                              | Purpose                                                                                    |
| ------------------------------------------------- | ------------------------------------------------------------------------------------------ |
| `~/.config/plasma-lyrics/plasma-lyricsd.ini`      | Daemon configuration (INI)                                                                 |
| `~/.local/share/plasma-lyrics/lyrics.db`          | Lyrics cache (SQLite), including per-song offsets, preferred sources and the global offset |
| `~/.local/share/plasma-lyrics/lyrics/`            | Default searchable local lyrics directory                                                  |
| `~/.local/share/plasma-lyrics/overrides/`         | Manual `.lrc` override directory (bilingual supported, see "Lyric sources" above)          |
| `~/.local/share/plasma-lyrics/plasma-lyricsd.log` | Optional log file (off by default)                                                         |
| `~/.cache/plasma-lyrics/amll-index.jsonl`         | AMLL metadata index cache                                                                  |
| `$XDG_RUNTIME_DIR/plasma-lyricsd/state.json`      | Atomic whole-song lyrics snapshot (the widget's only source for lyrics and playback state) |

A widget's appearance, text and other settings are changed on that widget's own
configuration pages; desktop and panel widgets do not affect each other.

## 🔧 Diagnostics

```sh
# Replay the whole matching process for one song offline (no music needs to be playing)
plasma-lyricsd --explain "Song title" "Artist"

# Diagnose a single lyric source; the playback platform can also be given to replay platform-specific matching
plasma-lyricsd --explain --provider amll --platform apple "Song title" "Artist"

# Give the track length (ms) to replay duration-dependent decisions, such as the translated-parenthetical titles above
plasma-lyricsd --explain --length-ms 215000 "Song title" "Artist"

# Follow the daemon log
journalctl --user -u plasma-lyricsd.service -f
```

The explanation prints the chain of lyric sources enabled in the current
configuration, each source's version-matching tier, and the rejection reasons.
When `--provider` names a source that is not enabled or not configured, it lists
the available sources instead.

The "Record debug details" checkbox under "Diagnostics" on the "Lyrics Service"
settings page (config key `logging/debug`, off by default) turns on debug-level
logging for the following seven categories, effective after the service restarts:

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
precedence over the config value. The daemon runs as a `systemd --user` service,
so the variable has to be written into that user instance with `set-environment`
and the service restarted before it takes effect:

```sh
systemctl --user set-environment QT_LOGGING_RULES="plasmalyrics.mpris.debug=true"
systemctl --user restart plasma-lyricsd

# to revert
systemctl --user unset-environment QT_LOGGING_RULES
systemctl --user restart plasma-lyricsd
```

With the KDE widget installed (`BUILD_PLASMOID=ON`), `kdebugsettings` shows eight
categories — the seven above plus the widget's own `plasmalyrics.config` (see
"Settings change log" below). "Record debug details" only governs the seven
above: they can be toggled individually in `kdebugsettings`, but only while that
checkbox is off — the checkbox takes precedence over kdebugsettings and, while
on, overrides any per-category debug toggle made there. `plasmalyrics.config` is
unaffected by that checkbox; it can only be toggled in `kdebugsettings` or via
`QT_LOGGING_RULES` set in plasmashell's own environment.

**Reading the log**: the line format depends on where the log goes. Under the
systemd journal each line is `category message` — journald records the timestamp
and level itself, so `journalctl` colors entries by level and
`journalctl --user -u plasma-lyricsd -p 4` shows only warnings and above. Run in a
terminal, the format is `[time] level category message`, colored by level;
redirected to a file or pipe it is the same `[time] level category message`
without color. In all three forms, and in the log file, category names drop the
`plasmalyrics.` prefix (`plasmalyrics.mpris` from the table above appears as
`mpris`), but `QT_LOGGING_RULES` and `kdebugsettings` still need the full name.
Every lyric resolve starts with a `#number` shared by all of its log lines; gaps
in the numbering are normal: whenever the daemon finds nothing playable to
resolve (no player at startup, the last player exiting, every remaining source
filtered out and not playing), it cancels the resolve in progress, which also
consumes a number. At info level, by default a resolve logs one line each at its
start, at the key steps along the way, and at its end:

```
#42 resolve: trigger=track-changed identity="Google Chrome" service=org.mpris.MediaPlayer2.plasma-browser-integration fingerprint="mediaSrc:0f3e…" platform=netease music=true title="劣等上等" artist="鏡音リン"
#42 cache miss
#42 search provider=local candidates=0 selected=none elapsed=3ms
#42 search provider=netease candidates=1 selected=1294899572 score=0.900 elapsed=1488ms
#42 fetched: netease/1294899572 lines=59 hasWords=false elapsed=560ms
#42 state=ok from=provider source=netease/1294899572 lines=59 tried=local elapsed=2061ms
```

`trigger=` on the start line gives the reason the resolve ran:

| Value | Meaning |
| --- | --- |
| `startup` | The service's first resolve after starting |
| `track-changed` | The same player moved to a new track |
| `player-changed` | The active player changed |
| `replay` | The same track looped into a new round |
| `research` | The widget menu's "Search for lyrics again" |
| `set-preferred` | A preferred lyric source was set for the current track |
| `clear-preferred` | The current track's preferred lyric source was cleared |

With "Record debug details" on, debug-level lines such as per-candidate match
scoring and cache-lookup details are printed as well.

**Settings change log**: saving the "Lyrics Service" or "Global settings" page,
every setting changed on the "Desktop appearance", "Panel appearance" and "Text"
pages, and the request and outcome of "Save and restart service" each log one
line, for example:

```
config changed store=db key=globalOffsetMs old="0" new="-300"
config changed store=applet applet=12 form=desktop key=desktopFontSize old="34" new="36"
```

The widget's copy lands in plasmashell's log; read it with
`journalctl --user QT_CATEGORY=plasmalyrics.config`. The daemon receives the same
line and logs it under `plasmalyrics.daemon`, so it also appears in
`journalctl --user -u plasma-lyricsd` and the optional log file — while the service
isn't running, the daemon's copy doesn't exist and only the widget's does.
`network/proxyUrl` is logged without credentials.

## 🛠️ Development checks

```sh
ctest --test-dir build --output-on-failure
/usr/lib/qt6/bin/qmllint --bare -I build/bin -I /usr/lib/qt6/qml \
  --unqualified disable --max-warnings 0 \
  frontend/plasmoid/package/contents/config/config.qml \
  frontend/plasmoid/package/contents/ui/*.qml \
  frontend/plasmoid/package/contents/ui/config/*.qml
sh frontend/plasmoid/translations/check-pot-freshness.sh
sh frontend/plasmoid/translations/check-message-coverage.sh
xmllint --noout frontend/plasmoid/package/contents/config/main.xml
QML2_IMPORT_PATH="$PWD/build/bin" plasmoidviewer -a io.github.swim233.plasma-lyrics -f planar
```

Use Qt 6's `/usr/lib/qt6/bin/qmllint`. Its arguments are the same as in CI plus
`--bare -I /usr/lib/qt6/qml`: on a machine that has this project's package
installed, qmllint otherwise resolves the project's QML module to the installed
system copy instead of the freshly built one in `build/bin`. The two translation
scripts check, respectively, that the committed `.pot` is in sync with the UI
strings in the source and that `messages.sh` covers every `.qml` file.

CI builds and runs the tests on both Arch Linux and Debian 13. On Arch it also
checks the translation catalogs, does a separate build and test run with
`-Wall -Wextra -Wpedantic -Werror`, runs qmllint, and builds the Arch package and
checks it with namcap; on Debian it builds the `.deb` package. Per-release changes
are listed in [CHANGELOG.md](CHANGELOG.md) (in Chinese).

## 📄 License

[GPL-2.0-only](LICENSE)
