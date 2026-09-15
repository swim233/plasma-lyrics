#!/bin/bash
# Exercise the real wrapper without installing packages or invoking namcap.
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

namcap() {
  case "$1" in
    clean.pkg.tar.zst) return 0 ;;
    allowed.pkg.tar.zst)
      echo "package W: Dependency included, but may not be needed ('plasma-workspace')" ;;
    finding.pkg.tar.zst) echo 'package E: unexpected dependency' ;;
    stderr.pkg.tar.zst) echo 'unexpected diagnostic' >&2 ;;
    failure.pkg.tar.zst) echo 'cannot analyze package' >&2; return 42 ;;
    silent.pkg.tar.zst) return 42 ;;
    allowed-failure.pkg.tar.zst)
      echo "package W: Dependency included, but may not be needed ('plasma-workspace')"
      return 42 ;;
    *) echo "unexpected namcap invocation: $1" >&2; return 99 ;;
  esac
}
export -f namcap

failures=0
check() {
  local expected=$1 output status=0
  shift
  output=$(bash "$HERE/namcap-check.sh" "$@" 2>&1) || status=$?
  if { [[ $expected == pass ]] && (( status == 0 )); } ||
     { [[ $expected == fail ]] && (( status != 0 )); }; then
    printf 'PASS: %s (%s)\n' "$*" "$expected"
  else
    printf 'FAIL: %s expected %s, exit=%s\n%s\n' "$*" "$expected" "$status" "$output" >&2
    failures=$((failures + 1))
  fi
}

check pass clean.pkg.tar.zst
check pass allowed.pkg.tar.zst
check fail finding.pkg.tar.zst
check fail stderr.pkg.tar.zst
check fail failure.pkg.tar.zst
check fail silent.pkg.tar.zst
check fail allowed-failure.pkg.tar.zst
check fail failure.pkg.tar.zst clean.pkg.tar.zst
check pass package-debug-1.pkg.tar.zst

if (( failures )); then
  printf '%s regression test(s) failed\n' "$failures" >&2
  exit 1
fi
