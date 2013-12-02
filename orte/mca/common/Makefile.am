#AM_CPPFLAGS = -I/usr/include
#LDADD = -L/usr/lib -lavro

AUTOMAKE_OPTIONS = subdir-objects

lib_LTLIBRARIES = libhamster_common.la

headers = hdclient.h \
	base/allocate.h \
	base/common.h \
	base/constants.h \
	base/finish.h \
	base/heartbeat.h \
	base/hnp_request_wrapper.h \
	base/launch.h \
	base/register.h \
	base/net_utils.h \
	base/str_utils.h

libhamster_common_la_SOURCES = \
	$(headers) \
	base/allocate.c \
	base/common.c \
	base/finish.c \
	base/heartbeat.c \
	base/hnp_request_wrapper.c \
	base/launch.c \
	base/register.c \
	base/net_utils.c \
	base/str_utils.c \
	base/hdclient.c
	
install-data-local:
	$(MKDIR_P) $(DESTDIR)$(sysconfdir)/schema/
	$(INSTALL_DATA) schema/AllocateRequestRecordAvro.avsc $(DESTDIR)$(sysconfdir)/schema/
	$(INSTALL_DATA) schema/AllocateResponseRecordAvro.avsc $(DESTDIR)$(sysconfdir)/schema/
	$(INSTALL_DATA) schema/FinishRequestRecordAvro.avsc $(DESTDIR)$(sysconfdir)/schema/
	$(INSTALL_DATA) schema/FinishResponseRecordAvro.avsc $(DESTDIR)$(sysconfdir)/schema/
	$(INSTALL_DATA) schema/HamsterHnpRequestRecordAvro.avsc $(DESTDIR)$(sysconfdir)/schema/
	$(INSTALL_DATA) schema/HeartBeatRequestRecordAvro.avsc $(DESTDIR)$(sysconfdir)/schema/
	$(INSTALL_DATA) schema/HeartBeatResponseRecordAvro.avsc $(DESTDIR)$(sysconfdir)/schema/
	$(INSTALL_DATA) schema/LaunchRequestRecordAvro.avsc $(DESTDIR)$(sysconfdir)/schema/
	$(INSTALL_DATA) schema/LaunchResponseRecordAvro.avsc $(DESTDIR)$(sysconfdir)/schema/
	$(INSTALL_DATA) schema/RegisterRequestRecordAvro.avsc $(DESTDIR)$(sysconfdir)/schema/
	$(INSTALL_DATA) schema/RegisterResponseRecordAvro.avsc $(DESTDIR)$(sysconfdir)/schema/

uninstall-local:
	rm -rf $(DESTDIR)$(sysconfdir)/schema
	
EXTRA_DIST = schema/AllocateRequestRecordAvro.avsc \
			schema/AllocateResponseRecordAvro.avsc \
			schema/FinishRequestRecordAvro.avsc \
			schema/FinishResponseRecordAvro.avsc \
			schema/HamsterHnpRequestRecordAvro.avsc \
			schema/HeartBeatRequestRecordAvro.avsc \
			schema/HeartBeatResponseRecordAvro.avsc \
			schema/LaunchRequestRecordAvro.avsc \
			schema/LaunchResponseRecordAvro.avsc \
			schema/RegisterRequestRecordAvro.avsc \
			schema/RegisterResponseRecordAvro.avsc
