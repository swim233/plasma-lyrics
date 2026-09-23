# Repository notes

`docs/DESIGN.md` is the source of truth. Keep `core/` free of QtNetwork and QtDBus;
provider integrations belong under `providers/`, MPRIS belongs under `daemon/`,
and every plasmoid instance must remain a read-only consumer of the atomic
snapshot except for explicit user configuration such as per-track offsets.

The browser integration's track id is constant and it never emits `Seeked`.
Never replace the media-source/metadata fingerprint or Position jump detection
with track-id-only logic.

Lint QML with `/usr/lib/qt6/bin/qmllint`, never the `qmllint` on `PATH` --
that one is Qt 5's and silently exits 0 on unknown types and undefined
property references, so "qmllint clean" from it means nothing. The Qt 6 one
reports `[missing-property]`, which is the only check covering `main.qml`:
it is a `PlasmoidItem` and cannot be instantiated by the QML test suite, so
nothing else catches a binding that references a property that no longer
exists.

On a machine that also has the `plasma-lyrics-git` AUR package installed
(this project's own daily-use install), `qmllint` resolves
`io.github.swim233.lyrics` against the system-installed
`/usr/lib/qt6/qml/io/github/swim233/lyrics/` copy ahead of a freshly built
`build/bin` -- `-I build/bin` does not win that race, and neither does the
`QML_IMPORT_PATH` environment variable. A property added since the last
`makepkg`/install then reads as a false `[missing-property]`. Add
`--bare -I build/bin -I /usr/lib/qt6/qml` (the second `-I` puts back
QtQuick/Kirigami, which `--bare` also drops) to get a result that matches
CI, which never has that package installed.

No `--` inside an XML comment in `frontend/plasmoid/package/contents/config/main.xml`
(the XML spec forbids it). KConfigLoader's parser stops at the first error and
returns without logging, so every `<entry>` after that point silently ceases to
exist -- the applet reads `undefined`, the config dialog writes 0/false/"" back
on Save, and the panel widget collapses to zero width. `tst_configschema` is the
only check that opens this file; keep it passing.

Every `add_test` must carry `ENVIRONMENT "LC_ALL=C.UTF-8"` (merge it into an
existing `ENVIRONMENT` list rather than adding a second `set_tests_properties`,
which would clobber the first). Without it each test binary emits Qt's
"Detected locale ... not UTF-8" warning, and because ctest collects output
through a pipe -- so stderr is not a tty -- Qt's default handler routes that
warning through `sd_journal_send()` into the developer's real journal. That call
looks at neither `DBUS_SESSION_BUS_ADDRESS` nor the XDG directories, so no
process-level isolation can intercept it; a full `ctest` run used to leave ~25
entries behind. The same applies when running a test binary directly instead of
through ctest -- set the variable yourself. Rationale also recorded in
`core/tests/CMakeLists.txt`.

Exception: `appstreamtest` needs no `ENVIRONMENT`. It is not one of this
repo's `add_test` calls -- ECM adds it in
`/usr/share/ECM/kde-modules/KDECMakeSettings.cmake`, and its command is
`cmake -P appstreamtest.cmake`, which runs no Qt binary at all (confirmed:
in a build directory that has never been installed from -- which is what CI
always has -- its only output is "Not installed yet, skipping"; a directory
that has been installed from takes a different branch and prints nothing).
The `sd_journal_send()` rationale above does not apply to it either way.

A test that simulates a crashing child must kill it with `SIGKILL`
(`kill -KILL $$` inside a `/bin/sh -c` command), never with a core-dumping
signal such as `SIGSEGV` or `SIGABRT`. `QProcess` reports `CrashExit` for
any signal death, so the test loses nothing. A core-dumping signal is piped
by the kernel to `systemd-coredump` on every run, which stores a core, writes
several journal entries and on Plasma also launches
`drkonqi-coredump-processor`; `RLIMIT_CORE=0` in the child only drops the
core file, the journal and DrKonqi traffic stay (confirmed: before
`tst_backendconfig` switched to `SIGKILL` it left 832 `coredumpctl` entries
in two weeks).

Commit messages are in Chinese with an English Conventional Commits prefix:
`type(scope): 中文主题`, where type is one of feat / fix / perf / docs /
chore / merge. The subject states what was implemented or fixed and the new
behaviour; the body is zero to two short declarative sentences stating only
the new behaviour -- no root-cause narration, no metaphor. Do not add
`Co-Authored-By` trailers. Example:

    fix(部件): 跑马灯失效时重置偏移

    偏移改为 LyricLine 的属性绑定，失效即归零。

README.md and release notes are written in Chinese; README.en.md carries the
English copy.

Release notes follow this format:
- Grouped by change type with English section headers, in this order:
  Breaking Changes → Added → Changed → Fixed → Removed → Security.
  Empty sections are omitted; the project is small enough that module
  grouping is skipped.
- One bullet per change, verb-first in Chinese (新增 / 更改 / 修复 / 移除),
  stating what changed and its user-visible behaviour; for fixes, state the
  symptom, not the internals. Breaking changes must include migration notes.
- Code elements (settings, commands, paths) in backticks.
- Major versions open with a one-line 概要; every release ends with a
  完整变更列表 compare link to the previous tag.

Release notes live in `CHANGELOG.md` at the repo root. CI extracts the
section whose heading matches the pushed tag and uses it as the GitHub
Release body; the release is created as a draft and published by hand. New
changes go under `## 未发布` until a tag is cut. The `v0.1.0` / `v0.1.1` /
`v0.2.0` entries were copied verbatim from the pre-existing GitHub Releases
via `gh release view` and must not be rewritten.

Cutting a release: rename `## 未发布` to `## v<x.y.z> - <YYYY-MM-DD>`, add
the 完整变更列表 compare link to the previous tag, open a fresh empty
`## 未发布` above it, bump `project(VERSION)` (and `metadata.json`, which the
build checks) to match, then tag `v<x.y.z>` on that commit. CI matches a
section by the tag name followed by ` - `, so the heading and the tag must
agree exactly.

`packaging/aur/PKGBUILD` (the `-git` package) is the only hand-maintained
PKGBUILD. `packaging/aur/generate.sh` derives the two release ones --
`plasma-lyrics` by sed, so `build()`/`check()`/`package()` stay
single-sourced, and `plasma-lyrics-bin` from a template with the `depends`
array spliced in verbatim. Never edit a generated PKGBUILD: a second copy of
`depends` is exactly how the released source package went four versions
without the `zlib` the daemon links directly. Run the script locally to see
what CI will produce.

`packaging/aur/namcap-check.sh` runs namcap on the built *package* and fails
on anything not on its allowlist. Do not point namcap at a PKGBUILD instead:
a PKGBUILD carries no ELF data, so that form of the check cannot see a
missing linkage at all, which is why the `zlib` gap survived a namcap that
was already running. namcap's output depends on what is installed on the
analysing machine -- in a bare container it resolves nothing and emits ~25
"uninstalled dependency" lines -- so it is only meaningful in a job that has
installed the package's `depends` first. Two findings are allowlisted, each
with its reason in the script; a third means the build fails.

After pushing a release tag, push both `plasma-lyrics` and
`plasma-lyrics-bin` to AUR by hand, from the `plasma-lyrics-<version>-aur.tar.gz`
release asset -- it holds one directory per package, each with its PKGBUILD
and `.SRCINFO`, to be extracted into that package's own AUR clone. CI only
generates and validates them; it holds no AUR credentials.
`plasma-lyrics-bin`'s `source=` points at the
`plasma-lyrics-<version>-x86_64-bin.tar.gz` asset, so that asset has to stay
published for as long as that PKGBUILD is live.

## Agent team workflow

Sizeable features are split by the lead into independent tasks with
disjoint file sets. Each task gets its own `dev` agent (opus) working in
its own git worktree under `/home/swim/code/desktop_lyrics-wt/<name>` on a
`feat/<name>` branch, all tasks running in parallel. Groundwork that several
tasks depend on is written by the lead first and committed on a base branch
the task branches start from. Each dev configures and builds in its own
worktree (`build/` inside it; ccache is installed) and commits on its branch
in the commit-message style above. When a dev merges the integration branch
into its own branch (for example to fix review findings on the integrated
tree), the merge subject names what came in, such as
`merge(外观): 字体目录分支合入集成分支的渲染与设置页改动`, never a bare
"同步集成分支". When every task is done the lead merges
the branches into one integration branch, re-verifies the whole tree itself
(`git status --short`, full `git diff --stat` against `main`, build, ctest,
qmllint), then hands the integrated tree to a single `reviewer` agent (opus)
for the final review. Findings go back to the responsible dev; only after
the reviewer passes it does the lead merge into `main` and remove the
worktrees and merged branches.

Nobody does destructive verification in a tree that holds uncommitted
changes: `git checkout -- <file>` there destroys the whole change, not just
the breakage that was introduced on purpose. Copy the tree to a scratch
directory first. No agent's report replaces the lead's own verification.
