# Repository notes

`DESIGN.md` is the source of truth. Keep `core/` free of QtNetwork and QtDBus;
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

After pushing a release tag, push the `plasma-lyrics` package to AUR by
hand. CI only generates and validates the PKGBUILD and `.SRCINFO`; it holds
no AUR credentials.
