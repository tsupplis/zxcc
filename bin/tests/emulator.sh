#!/bin/sh
set -eu
binary=${1:-./zxcc}
case "$binary" in
    /*) ;;
    *) binary="$(pwd)/$binary" ;;
esac
root=${ZXCC_SOURCE_ROOT:-"$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)"}
work=$(mktemp -d "${TMPDIR:-/tmp}/zxcc-emulator-tests.XXXXXX")
longdir=""
cleanup()
{
    rm -f "$work/bin/zxcc" "$work/bin/bios.bin" "$work/empty/bios.bin" "$work/run/probe.com" "$work/run/out" "$work/run/err"
    if [ -n "$longdir" ]; then
        rm -f "$longdir/zxcc" "$longdir/bios.bin"
        while [ "$longdir" != "$work" ]; do
            rmdir "$longdir"
            longdir=$(dirname "$longdir")
        done
    fi
    if [ -d "$work/run/dir with spaces" ]; then
        rmdir "$work/run/dir with spaces"
    fi
    rmdir "$work/bin" "$work/empty" "$work/run" "$work"
}
trap cleanup EXIT HUP INT TERM
mkdir "$work/bin" "$work/empty" "$work/run"
cp "$binary" "$work/bin/zxcc"
cp "$root/Z80/bios.bin" "$work/bin/bios.bin"
cd "$work/run"
printf '\016\011\021\013\001\315\005\000\303\000\000OK$' > probe.com
unset CPMDIR80 LIBDIR80 INCDIR80
export BINDIR80="$work/empty"
passed=0
runner="$work/bin/zxcc"
expect_ok()
{
    "$runner" "$@" >out 2>err
    grep -q OK out
    passed=$((passed + 1))
}
expect_fail()
{
    message=$1
    shift
    if "$runner" "$@" >out 2>err; then
        echo "FAIL: emulator accepted invalid input" >&2
        exit 1
    fi
    grep -q "$message" err
    passed=$((passed + 1))
}
expect_ok probe.com
expect_ok probe
expect_ok probe.com ""
boundary=$(awk 'BEGIN { for (i=0; i<126; i++) printf "x" }')
expect_ok probe.com "-$boundary"
expect_fail "command tail exceeds 127" probe.com "-${boundary}x"
long=$(awk 'BEGIN { for (i=0; i<5000; i++) printf "x" }')
expect_fail "command tail exceeds 127" probe.com "-$long"
expect_fail "path is too long" "$long"
expect_fail "supported path length" probe.com "$long"
mkdir "$work/run/dir with spaces"
expect_ok probe.com "$work/run/dir with spaces/file.c"
rmdir "$work/run/dir with spaces"
component=$(awk 'BEGIN { for (i=0; i<120; i++) printf "d" }')
longdir="$work"
for n in 1 2 3 4 5; do
    mkdir "$longdir/$component"
    longdir="$longdir/$component"
done
cp "$binary" "$longdir/zxcc"
cp "$root/Z80/bios.bin" "$longdir/bios.bin"
runner="$longdir/zxcc"
expect_ok probe.com
runner="$work/bin/zxcc"
paths=""
for n in 0 1 2 3 4 5 6 7 8 9 10 11 12; do
    paths="$paths /mapping$n/file.c"
done
# These intentionally distinct, space-free paths consume the twelve free drives.
set -- $paths
expect_fail "No free drive mapping" probe.com "$@"
export BINDIR80="$long"
expect_fail "directory path is too long" probe.com
export BINDIR80="$work/empty"
rm "$work/bin/bios.bin"
expect_fail "Cannot locate bios.bin" probe.com
printf 'Emulator integration tests: %s passed\n' "$passed"
