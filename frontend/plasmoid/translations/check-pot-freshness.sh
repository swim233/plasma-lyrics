#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
#
# Regenerates the .pot from the current source and compares it against
# what's checked in at HEAD, so a QML string edited without a matching
# `sh messages.sh` re-run -- or a keyword flag dropping off messages.sh
# itself, as happened once with i18np, see its own comment -- gets caught.
#
# The regenerated copy is written outside the repository (via messages.sh's
# own `podir` override, same trick check-message-coverage.sh uses) instead
# of letting messages.sh overwrite the checked-in .pot in place. A checker
# must not modify the thing it is checking: writing into the repo would
# leave a stray, uncommitted change behind after every local run -- and
# specifically a change to POT-Creation-Date, the one field this script
# deliberately does not compare (see below), so this exact check would
# never flag the stray write as a problem, and someone could commit that
# timestamp noise without ever finding out where it came from.
#
# The one thing this deliberately does NOT compare is the
# POT-Creation-Date header line. xgettext stamps it with the current
# wall-clock time (minute resolution) on every single run, with no flag to
# suppress or pin it -- and --omit-header would throw away the rest of the
# header (Content-Type/charset) along with it. A byte-for-byte comparison
# would therefore differ on this line on every CI run no matter how long
# ago the .pot was actually regenerated, which is not a real drift to
# report: it would just make this check permanently red until someone
# neuters it. Every other line -- #: location comments (these are what
# catch a stale re-run: a source edit that shifted line numbers without
# anyone re-running messages.sh) and msgid/msgid_plural/msgstr text -- is
# still compared exactly; only POT-Creation-Date is filtered out of both
# sides before the diff.
set -eu

script_dir=$(cd "$(dirname "$0")" && pwd)
repo_root=$(cd "$script_dir/../../.." && pwd)
catalog="plasma_applet_io.github.swim233.plasma-lyrics"
pot_relpath="frontend/plasmoid/translations/templates/$catalog.pot"

workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT INT TERM

(
    cd "$repo_root"
    podir="$workdir" sh frontend/plasmoid/translations/messages.sh
)

git -C "$repo_root" show "HEAD:$pot_relpath" | grep -v '^"POT-Creation-Date:' > "$workdir/committed.pot"
grep -v '^"POT-Creation-Date:' "$workdir/$catalog.pot" > "$workdir/regenerated.pot"

if ! diff -u "$workdir/committed.pot" "$workdir/regenerated.pot"; then
    echo "check-pot-freshness: the checked-in .pot does not match what messages.sh produces from the current source (POT-Creation-Date excluded from the comparison above)." >&2
    echo "Run: sh frontend/plasmoid/translations/messages.sh -- and commit the result." >&2
    exit 1
fi

echo "check-pot-freshness: checked-in .pot matches messages.sh output (POT-Creation-Date excluded)."
