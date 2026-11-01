#!/bin/sh
# Run against built wrappers; no emulator, CP/M binaries, or test framework.
set -eu
builddir=$(cd "$1" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/zxcc-wrapper-tests.XXXXXX")
trap 'rm -rf "$work"' 0
trap 'exit 1' 1 2 3 15
mkdir "$work/child bin" "$work/empty"
cp "$builddir/wrapper-test-child" "$work/child bin/zxcc"
child="$builddir/wrapper-test-child"
export ZXCC_TEST_OUTPUT="$work/actual"
unset ZXCC_TEST_SIGNAL
cases=0

expect()
{
	"$child" --record "$work/expected" "$@"
}

check()
{
	tool=$1
	wanted=$2
	shift 2
	got=0
	PATH="$work/child bin:$PATH" ZXCC_TEST_EXIT="$wanted" \
		"$builddir/$tool" "$@" || got=$?
	if test "$got" -ne "$wanted"; then
		echo "$tool: expected exit $wanted, got $got" >&2
		exit 1
	fi
	cmp "$work/expected" "$work/actual"
	cases=$((cases + 1))
}

long=0123456789abcdef
for i in 1 2 3 4 5 6 7 8; do long=$long$long; done
literal='dir with spaces/a;$HOME`echo nope`"quote'\''*.c'
backslashes='dir with spaces\embedded\"quote\'
for tool in zxc zxas zxlink zxlibr zxobjtohex; do
	case $tool in
		zxc) program=c.com ;;
		zxas) program=zas.com ;;
		zxlink) program=linq.com ;;
		zxlibr) program=libr.com ;;
		zxobjtohex) program=objtohex.com ;;
	esac
	set -- "$program"
	if test "$tool" = zxc; then set -- "$@" --IC:; fi
	expect "$@"
	check "$tool" 0
	check "$tool" 7
	check "$tool" 255
	if test "$tool" = zxlibr; then
		expect "$@" -r "$literal" "$backslashes" "" "$long" "$long"
		check "$tool" 0 r "$literal" "$backslashes" "" "$long" "$long"
		expect "$@" - "" -
		check "$tool" 0 "" "" -
		expect "$@" --x -y +file
		check "$tool" 0 -x -y +file
	else
		expect "$@" "$literal" "$backslashes" "" "$long" "$long" --x +file --
		check "$tool" 0 "$literal" "$backslashes" "" "$long" "$long" -x +file -
		expect "$@" "" --
		check "$tool" 0 "" -
	fi
	# Many short arguments as well as individual long ones.
	set -- x
	i=0
	while test "$i" -lt 600; do set -- "$@" "file$i"; i=$((i + 1)); done
	if test "$tool" = zxlibr; then
		shift
		expect "$program" -x "$@"
		check "$tool" 0 x "$@"
	elif test "$tool" = zxc; then
		expect "$program" --IC: "$@"
		check "$tool" 0 "$@"
	else
		expect "$program" "$@"
		check "$tool" 0 "$@"
	fi
	got=0
	PATH="$work/empty" "$builddir/$tool" 2>"$work/missing-child.err" || got=$?
	if test "$got" -ne 127 || ! grep -q 'cannot execute zxcc:' "$work/missing-child.err"; then
		echo "$tool: expected missing-child diagnostic and exit 127, got $got" >&2
		cat "$work/missing-child.err" >&2
		exit 1
	fi
	cases=$((cases + 1))
	got=0
	PATH="$work/child bin:$PATH" ZXCC_TEST_SIGNAL=1 \
		"$builddir/$tool" || got=$?
	test "$got" -eq 143
	cases=$((cases + 1))
done

expect c.com --IC: --e '+out dir/a.com' --f '+sym dir/a.sym' \
	--i '+inc dir/' --m '+map dir/a.map' --cr '+xref dir/a.crf' \
	--cr +mixed --cr +upper --cr +other --e --cr --c
check zxc 0 '-Eout dir/a.com' '-fsym dir/a.sym' '-Iinc dir/' \
	'-mmap dir/a.map' '-crxref dir/a.crf' -cRmixed -CRupper -Crother -e -cr -c
expect c.com --IC: --e +a --f +b --i +c --m +d --cr +e
check zxc 0 -ea -Fb -ic -Md -CRe
expect zas.com --o '+out dir/a.obj' --l '+list dir/a.lst' --o --l
check zxas 0 '-Oout dir/a.obj' '-llist dir/a.lst' -o -l
expect linq.com --o '+out dir/a.com' --m '+map dir/a.map' \
	--d '+data dir/a.dat' --o --m --d
check zxlink 0 '-oout dir/a.com' '-Mmap dir/a.map' '-Ddata dir/a.dat' -o -m -d
echo "wrapper regression tests: $cases cases passed"
