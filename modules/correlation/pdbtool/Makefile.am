bin_PROGRAMS				+= modules/correlation/pdbtool/pdbtool

EXTRA_DIST += modules/correlation/pdbtool/CMakeLists.txt

PDBTOOL_BUILTIN_MODULES = modules/syslogformat/libsyslogformat.la modules/basicfuncs/libbasicfuncs.la

modules_correlation_pdbtool_pdbtool_CPPFLAGS=	\
	$(AM_CPPFLAGS)				\
	-I$(top_srcdir)/modules/correlation	\
	-I$(top_builddir)/modules/correlation

modules_correlation_pdbtool_pdbtool_SOURCES =	\
	modules/correlation/pdbtool/pdbtool.c

if ENABLE_BUILTIN_MODULES

nodist_modules_correlation_pdbtool_pdbtool_SOURCES = modules/correlation/pdbtool/pdbtool-modules.c
modules_correlation_pdbtool_pdbtool_LDADD	=	\
	$(PDBTOOL_BUILTIN_MODULES) \
	$(top_builddir)/lib/libsyslog-ng.la	\
	$(top_builddir)/modules/correlation/libsyslog-ng-patterndb.la \
	@TOOL_DEPS_LIBS@

modules/correlation/pdbtool/pdbtool-modules.c: syslog-ng-config.h
	$(AM_V_at) $(mkinstalldirs) $(dir $@)
	$(AM_V_GEN) $(PYTHON) $(top_srcdir)/scripts/gen-builtin-modules.py $(PDBTOOL_BUILTIN_MODULES) > $@

BUILT_SOURCES += modules/correlation/pdbtool/pdbtool-modules.c
CLEANFILES += modules/correlation/pdbtool/pdbtool-modules.c

else

modules_correlation_pdbtool_pdbtool_LDADD	=	\
	$(top_builddir)/lib/libsyslog-ng.la	\
	$(top_builddir)/modules/correlation/libsyslog-ng-patterndb.la \
	@TOOL_DEPS_LIBS@

endif
