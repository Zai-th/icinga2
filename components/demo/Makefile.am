## Process this file with automake to produce Makefile.in

pkglib_LTLIBRARIES = \
	libdemo.la

EXTRA_DIST = \
	demo-type.conf

.conf.cpp: $(top_builddir)/tools/mkembedconfig/mkembedconfig.c
	$(top_builddir)/tools/mkembedconfig/mkembedconfig $< $@

libdemo_la_SOURCES = \
	democomponent.cpp \
	democomponent.h \
	demo-type.cpp

libdemo_la_CPPFLAGS = \
	$(LTDLINCL) \
	$(BOOST_CPPFLAGS) \
	-I${top_srcdir}/lib \
	-I${top_srcdir}/components

libdemo_la_LDFLAGS = \
	$(BOOST_LDFLAGS) \
	-module \
	-no-undefined \
	@RELEASE_INFO@ \
	@VERSION_INFO@

libdemo_la_LIBADD = \
	$(BOOST_SIGNALS_LIB) \
	$(BOOST_THREAD_LIB) \
	$(BOOST_SYSTEM_LIB) \
	${top_builddir}/lib/base/libbase.la \
	${top_builddir}/lib/config/libconfig.la \
	${top_builddir}/lib/icinga/libicinga.la
