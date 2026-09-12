#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
#
# Confirms every .qml file under frontend/plasmoid/package/contents is
# actually handed to xgettext by messages.sh. messages.sh feeds xgettext
# three glob arguments (config/config.qml, ui/*.qml, ui/config/*.qml); none
# of those globs is recursive, so a .qml file placed in any *other*
# subdirectory of ui/ -- one that does not exist yet today -- is silently
# skipped and its i18n() strings never reach the .pot. Regenerating the
# .pot and diffing it against the checked-in copy (see the CI step next to
# this one) cannot catch that: a skipped file means fewer strings go in on
# both the "before" and the freshly generated side, so the diff comes back
# clean. Only walking the source tree and checking each file against what
# xgettext actually received catches it.
#
# To avoid keeping a second copy of messages.sh's glob list here (which
# would just drift from the original the next time someone edits one but
# not the other), this script never reads messages.sh's globs at all. It
# runs messages.sh for real -- from the repo root, so the shell that
# interprets messages.sh expands `ui/*.qml` etc. against the real
# filesystem exactly as it does in production -- and intercepts the
# xgettext calls with a stub on PATH that only logs its argv instead of
# scanning files. Whatever file paths show up in that log *are* the set
# messages.sh actually asked xgettext to cover, straight from the source of
# truth, no transcription involved. --output is redirected to a scratch
# directory (via messages.sh's own `podir` override) so this never touches
# the checked-in .pot/.po.
set -eu

script_dir=$(cd "$(dirname "$0")" && pwd)
repo_root=$(cd "$script_dir/../../.." && pwd)
package_contents="$repo_root/frontend/plasmoid/package/contents"

workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT INT TERM

mkdir -p "$workdir/bin"
xgettext_log="$workdir/xgettext-args.log"
: > "$xgettext_log"

# Stub xgettext: record argv, then leave a plausible output file behind so
# the rest of messages.sh (--join-existing, and the trailing sed -i) still
# has something to operate on.
cat > "$workdir/bin/xgettext" <<'SHIM'
#!/bin/sh
set -eu
for arg in "$@"; do
    printf '%s\n' "$arg" >> "$XGETTEXT_ARGS_LOG"
    case "$arg" in
        --output=*) : > "${arg#--output=}" ;;
    esac
done
SHIM
chmod +x "$workdir/bin/xgettext"

(
    cd "$repo_root"
    PATH="$workdir/bin:$PATH" \
        XGETTEXT_ARGS_LOG="$xgettext_log" \
        podir="$workdir/podir" \
        sh "$script_dir/messages.sh"
)

if [ ! -s "$xgettext_log" ]; then
    echo "check-message-coverage: the xgettext stub was never invoked -- messages.sh no longer calls xgettext the way this check expects it to" >&2
    exit 1
fi

covered_list="$workdir/covered.txt"
grep -E '\.qml$' "$xgettext_log" | sort -u > "$covered_list" || true

actual_list="$workdir/actual.txt"
find "$package_contents" -name '*.qml' -print \
    | sed "s|^$repo_root/||" \
    | sort -u > "$actual_list"

missing_list="$workdir/missing.txt"
comm -23 "$actual_list" "$covered_list" > "$missing_list" || true

if [ -s "$missing_list" ]; then
    echo "check-message-coverage: the following .qml files are not covered by any messages.sh xgettext glob:" >&2
    sed 's/^/  /' "$missing_list" >&2
    echo "messages.sh's globs are not recursive; a .qml file in a ui/ subdirectory that isn't ui/config/ needs a new glob argument (or a recursive rewrite) added there." >&2
    exit 1
fi

echo "check-message-coverage: all $(wc -l < "$actual_list" | tr -d ' ') .qml files under frontend/plasmoid/package/contents are covered."
