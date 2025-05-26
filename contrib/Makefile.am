dist_sbin_SCRIPTS += \
	contrib/syslog-ng-debun

EXTRA_DIST += \
	contrib/README \
	\
	contrib/README.syslog-ng-debun \
	contrib/syslog-ng-debun \
	\
	contrib/syslog2ng \
	contrib/syslog-mc \
	\
	contrib/syslog-ng.conf.doc \
	contrib/relogger.pl \
	\
	contrib/Brewfile \
	\
	contrib/aix-packaging/syslog-ng.conf \
	\
	contrib/lfs-packaging/Makefile.lfs \
	contrib/lfs-packaging/README.lfs \
	contrib/lfs-packaging/syslog-ng.init.lfs \
	\
	contrib/cygwin-packaging/cygwin-postinstall \
	contrib/cygwin-packaging/syslog-ng-config \
	contrib/cygwin-packaging/syslog-ng.sh \
	\
	contrib/freebsd-packaging/syslog-ng.rc.d \
	contrib/freebsd-packaging/syslog-ng.conf.example \
	contrib/openbsd-packaging/syslog-ng.conf \
	\
	contrib/selinux/README \
	contrib/selinux/src/root_unsafe/syslog_ng.el6.fc.in \
	contrib/selinux/src/root_unsafe/syslog_ng.el789.fc.in \
	contrib/selinux/src/root_unsafe/syslog_ng.el5.fc.in \
	contrib/selinux/src/root_safe/syslog_ng.el6.fc.in \
	contrib/selinux/src/root_safe/syslog_ng.el789.fc.in \
	contrib/selinux/src/root_safe/syslog_ng.el5.fc.in \
	contrib/selinux/src/syslog_ng.el6.0to4.te.in \
	contrib/selinux/src/syslog_ng.el7.te.in \
	contrib/selinux/src/syslog_ng.el8.te.in \
	contrib/selinux/src/syslog_ng.el9.te.in \
	contrib/selinux/src/syslog_ng.module.version \
	contrib/selinux/src/syslog_ng.el6.5up.te.in \
	contrib/selinux/src/syslog_ng.el5.te.in \
	contrib/selinux/labels.txt \
	contrib/selinux/syslog_ng.sh \
	\
	contrib/apparmor/opt.syslog-ng.sbin.syslog-ng \
	\
	contrib/systemd/syslog-ng@.service \
	contrib/systemd/syslog-ng@default \
	\
	contrib/scripts/config-graph-json-to-dot.py

if ENABLE_SYSTEMD_UNIT_INSTALL
systemdsystemunit_DATA = contrib/systemd/syslog-ng@.service
endif
