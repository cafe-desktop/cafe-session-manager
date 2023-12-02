bin_PROGRAMS = cafe-session-properties

AM_CPPFLAGS =						\
	$(SESSION_PROPERTIES_CFLAGS)			\
	-I$(top_srcdir)/cafe-session			\
	-DLOCALE_DIR=\""$(datadir)/locale"\"		\
	-DCTKBUILDER_DIR=\""$(pkgdatadir)"\"		\
	$(DISABLE_DEPRECATED_CFLAGS)

AM_CFLAGS = $(WARN_CFLAGS)

cafe_session_properties_SOURCES =			\
	main.c						\
	csm-properties-dialog.h				\
	csm-properties-dialog.c				\
	csm-app-dialog.h				\
	csm-app-dialog.c				\
	gsp-app.h					\
	gsp-app.c					\
	gsp-app-manager.h				\
	gsp-app-manager.c				\
	gsp-keyfile.h					\
	gsp-keyfile.c

cafe_session_properties_LDADD =			\
	$(SESSION_PROPERTIES_LIBS)			\
	$(top_builddir)/cafe-session/libgsmutil.la

-include $(top_srcdir)/git.mk
