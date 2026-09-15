#!/bin/bash
# Fails on any namcap finding that is not on the allowlist below.
#
# namcap's output depends on what is installed on the analysing machine: in a
# bare container it resolves nothing and emits ~25 "uninstalled dependency"
# lines, so this is only meaningful in a job that has installed the package's
# depends first. Run it there, on the built package -- never on the PKGBUILD,
# which carries no ELF data and therefore cannot see a missing linkage at all.
# That blind spot is how the released source package went four versions without
# the zlib the daemon links directly.
set -uo pipefail

# Every entry needs a reason. Anything not listed fails the build.
#
#   plasma-workspace -- the host dependency. Nothing in the package links or
#   imports it (every QML import resolves to libplasma, kdeclarative, ksvg,
#   kirigami or qt6-declarative); it is here because a plasmoid without
#   plasmashell has nothing to run in. kdeplasma-addons declares it the same
#   way.
#
#   libplasmalyricssource.so -- installed to /usr/lib with an unversioned
#   soname, so no package owns it in namcap's eyes and it cannot attribute it
#   to the package under analysis either. Moving it into a private directory
#   would silence this; that is a deliberate open decision, not an oversight.
ALLOW=(
  "Dependency included, but may not be needed ('plasma-workspace')"
  "Referenced library 'libplasmalyricssource.so' is an uninstalled dependency"
)

status=0
for pkg in "$@"; do
  # The container's makepkg.conf enables debug, so a -debug- package sits next
  # to every real one. It is never shipped and namcap has nothing useful to say
  # about it.
  case $pkg in *-debug-*) echo "== skip $pkg"; continue;; esac
  echo "== namcap $pkg"
  # A failed analysis must fail the check even with empty or allowlisted output.
  # Inspect stderr as well: diagnostics there are not a successful clean scan.
  if out=$(namcap "$pkg" 2>&1); then
    :
  else
    code=$?
    echo "namcap: analysis failed for $pkg (exit $code)" >&2
    status=1
  fi
  printf '%s\n' "$out"
  while IFS= read -r line; do
    [ -n "$line" ] || continue
    for a in "${ALLOW[@]}"; do
      case $line in *"$a"*) continue 2;; esac
    done
    echo "namcap: unexpected finding -> $line" >&2
    status=1
  done <<< "$out"
done

if [ "$status" -ne 0 ]; then
  echo "namcap reported findings that are not on the allowlist in $0" >&2
fi
exit "$status"
