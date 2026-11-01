
AUTOMAKE_OPTIONS=subdir-objects
bin_PROGRAMS=zxcc zxc zxas zxlibr zxlink zxobjtohex

zxc_SOURCES=zxc.c wrapper.h
zxas_SOURCES=zxas.c wrapper.h
zxlink_SOURCES=zxlink.c wrapper.h
zxlibr_SOURCES=zxlibr.c wrapper.h
zxobjtohex_SOURCES=zxobjtohex.c wrapper.h
zxcc_SOURCES=zxbdos.c    zxcbdos.c  zxdbdos.c   zxcc.c             \
             z80.c       z80.h      z80ops.h    cbops.h    edops.h \
             zxbdos.h    zxcc.h	    zxcbdos.h   zxdbdos.h

check_PROGRAMS=wrapper-test-child
wrapper_test_child_SOURCES=tests/wrapper-test-child.c
EXTRA_DIST=tests/wrappers.sh tests/emulator.sh

check-local: zxcc zxc zxas zxlink zxlibr zxobjtohex $(check_PROGRAMS)
	$(SHELL) "$(srcdir)/tests/wrappers.sh" "$(abs_builddir)"
	ZXCC_SOURCE_ROOT="$(abs_srcdir)/.." $(SHELL) "$(srcdir)/tests/emulator.sh" "$(abs_builddir)/zxcc"
