if ENABLE_ENV_WRAPPER
libexec_PROGRAMS		+= syslog-ng/syslog-ng
sbin_PROGRAMS			+= syslog-ng/syslog-ng-wrapper
else
sbin_PROGRAMS			+= syslog-ng/syslog-ng
endif

if ENABLE_BUILTIN_MODULES
# monolithic linking, all compiled plugins are linked into the main executable.

syslog_ng_syslog_ng_SOURCES	= syslog-ng/main.c syslog-ng/builtin-modules.c
syslog_ng_syslog_ng_LDADD	= $(module_LTLIBRARIES) lib/libsyslog-ng.la @SYSLOGNG_DEPS_LIBS@
syslog_ng_syslog_ng_DEPENDENCIES= $(module_LTLIBRARIES)

else
syslog_ng_syslog_ng_SOURCES	= syslog-ng/main.c
syslog_ng_syslog_ng_LDADD       = lib/libsyslog-ng.la @SYSLOGNG_DEPS_LIBS@
endif

syslog_ng_syslog_ng_LINK	=  @SYSLOGNG_LINK@

syslog_ng_syslog_ng_wrapper_SOURCES	= syslog-ng/wrapper.c

syslog-ng-install-exec-hook:
if ENABLE_ENV_WRAPPER
	mv $(DESTDIR)/$(sbindir)/syslog-ng-wrapper $(DESTDIR)/$(sbindir)/syslog-ng
endif
	$(mkinstalldirs) $(DESTDIR)/$(localstatedir)

INSTALL_EXEC_HOOKS			+= syslog-ng-install-exec-hook

BUILT_SOURCES += syslog-ng/builtin-modules.c
CLEANFILES += syslog-ng/builtin-modules.c
EXTRA_DIST += syslog-ng/CMakeLists.txt

syslog-ng syslog-ng/: ${libexec_PROGRAMS} ${sbin_PROGRAMS}
.PHONY: syslog-ng/

syslog-ng/builtin-modules.c: syslog-ng-config.h
	$(AM_V_at) $(mkinstalldirs) $(dir $@)
	$(AM_V_GEN) $(PYTHON) $(top_srcdir)/scripts/gen-builtin-modules.py $(module_LTLIBRARIES) > $@
