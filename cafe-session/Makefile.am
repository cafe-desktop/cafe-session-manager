bin_PROGRAMS = cafe-session
noinst_LTLIBRARIES = libgsmutil.la
noinst_PROGRAMS = 		\
	test-client-dbus	\
	test-inhibit

AM_CPPFLAGS =					\
	$(CAFE_SESSION_CFLAGS)		\
	$(SYSTEMD_CFLAGS)			\
	$(LIBELOGIND_CFLAGS)			\
	$(DISABLE_DEPRECATED_CFLAGS)

AM_CFLAGS = $(WARN_CFLAGS)

cafe_session_SOURCES =				\
	csm-app.h				\
	csm-app.c				\
	csm-autostart-app.h			\
	csm-autostart-app.c			\
	csm-client.c				\
	csm-client.h				\
	csm-xsmp-client.h			\
	csm-xsmp-client.c			\
	csm-dbus-client.h			\
	csm-dbus-client.c			\
	csm-marshal.h				\
	csm-marshal.c				\
	csm-consolekit.c			\
	csm-consolekit.h			\
	csm-systemd.c 				\
	csm-systemd.h				\
	csm-logout-dialog.h			\
	csm-logout-dialog.c			\
	csm-inhibit-dialog.h			\
	csm-inhibit-dialog.c			\
	gs-idle-monitor.h			\
	gs-idle-monitor.c			\
	csm-presence.h				\
	csm-presence.c				\
	cdm.h					\
	cdm.c					\
	cdm-signal-handler.h			\
	cdm-signal-handler.c			\
	cdm-log.h				\
	cdm-log.c				\
	msm-gnome.c				\
	msm-gnome.h				\
	main.c					\
	csm-store.h				\
	csm-store.c				\
	csm-inhibitor.h				\
	csm-inhibitor.c				\
	csm-manager.c				\
	csm-manager.h				\
	csm-session-save.c			\
	csm-session-save.h			\
	csm-xsmp-server.c			\
	csm-xsmp-server.h

cafe_session_CPPFLAGS =			\
	$(AM_CPPFLAGS)				\
	$(X11_CFLAGS)                           \
	$(SM_CFLAGS)				\
	$(ICE_CFLAGS)				\
	$(XEXT_CFLAGS)				\
	-I$(top_srcdir)/egg			\
	-DLOCALE_DIR=\""$(datadir)/locale"\"	\
	-DDATA_DIR=\""$(datadir)/cafe-session"\" \
	-DLIBEXECDIR=\"$(libexecdir)\"		\
	-DCTKBUILDER_DIR=\""$(pkgdatadir)"\"	\
	-DI_KNOW_THE_DEVICEKIT_POWER_API_IS_SUBJECT_TO_CHANGE

cafe_session_LDADD =				\
	libgsmutil.la 				\
	$(X11_LIBS)                             \
	$(top_builddir)/egg/libeggdesktopfile.la \
	$(SM_LIBS)				\
	$(ICE_LIBS)				\
	$(XRENDER_LIBS)				\
	$(XTEST_LIBS)				\
	$(XEXT_LIBS)				\
	$(CAFE_SESSION_LIBS)			\
	$(SYSTEMD_LIBS)				\
	$(LIBELOGIND_LIBS)			\
	$(EXECINFO_LIBS)

libgsmutil_la_SOURCES =				\
	csm-util.c				\
	csm-util.h

libgsmutil_la_LIBADD = 				\
	$(CAFE_SESSION_LIBS)

test_inhibit_SOURCES = test-inhibit.c
test_inhibit_LDADD = $(CAFE_SESSION_LIBS)

test_client_dbus_SOURCES = test-client-dbus.c
test_client_dbus_LDADD = $(CAFE_SESSION_LIBS)

csm-marshal.c: csm-marshal.list
	$(AM_V_GEN)echo "#include \"csm-marshal.h\"" > $@ && \
	$(GLIB_GENMARSHAL) $< --prefix=gsm_marshal --body >> $@

csm-marshal.h: csm-marshal.list
	$(AM_V_GEN)$(GLIB_GENMARSHAL) $< --prefix=gsm_marshal --header > $@

csm-manager-glue.h: org.gnome.SessionManager.xml Makefile.am
	$(AM_V_GEN)dbus-binding-tool --prefix=gsm_manager --mode=glib-server --output=csm-manager-glue.h $(srcdir)/org.gnome.SessionManager.xml

csm-client-glue.h: org.gnome.SessionManager.Client.xml Makefile.am
	$(AM_V_GEN)dbus-binding-tool --prefix=gsm_client --mode=glib-server --output=csm-client-glue.h $(srcdir)/org.gnome.SessionManager.Client.xml

csm-app-glue.h: org.gnome.SessionManager.App.xml Makefile.am
	$(AM_V_GEN)dbus-binding-tool --prefix=gsm_app --mode=glib-server --output=csm-app-glue.h $(srcdir)/org.gnome.SessionManager.App.xml

csm-inhibitor-glue.h: org.gnome.SessionManager.Inhibitor.xml Makefile.am
	$(AM_V_GEN)dbus-binding-tool --prefix=gsm_inhibitor --mode=glib-server --output=csm-inhibitor-glue.h $(srcdir)/org.gnome.SessionManager.Inhibitor.xml

csm-presence-glue.h: org.gnome.SessionManager.Presence.xml Makefile.am
	$(AM_V_GEN)dbus-binding-tool --prefix=gsm_presence --mode=glib-server --output=csm-presence-glue.h $(srcdir)/org.gnome.SessionManager.Presence.xml

BUILT_SOURCES =			\
	csm-marshal.c		\
	csm-marshal.h		\
	csm-manager-glue.h	\
	csm-presence-glue.h	\
	csm-inhibitor-glue.h	\
	csm-client-glue.h	\
	csm-app-glue.h

EXTRA_DIST =						\
	README						\
	csm-marshal.list				\
	org.gnome.SessionManager.xml			\
	org.gnome.SessionManager.App.xml		\
	org.gnome.SessionManager.Client.xml		\
	org.gnome.SessionManager.ClientPrivate.xml	\
	org.gnome.SessionManager.Inhibitor.xml		\
	org.gnome.SessionManager.Presence.xml

CLEANFILES =	\
	$(BUILT_SOURCES)

-include $(top_srcdir)/git.mk
