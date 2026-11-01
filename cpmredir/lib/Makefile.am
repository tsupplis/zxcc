## Process this file with automake to produce Makefile.in

AM_CPPFLAGS=-I$(top_srcdir)/include

lib_LIBRARIES=libcpmredir.a
libcpmredir_a_SOURCES = cpmdrv.c	cpmint.h	cpmredir.c	util.c \
			cpmglob.c	cpmparse.c	drdos.c	track.c	xlt.c

check_PROGRAMS=testredir
testredir_SOURCES=testredir.c
testredir_LDADD=libcpmredir.a

check-local: testredir
	./testredir
