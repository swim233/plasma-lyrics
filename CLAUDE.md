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
