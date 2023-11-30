bin_PROGRAMS = cafe-session-save cafe-session-inhibit
libexec_PROGRAMS = \
	cafe-session-check-accelerated \
	cafe-session-check-accelerated-gl-helper

AM_CPPFLAGS =					\
	$(MATE_SESSION_CFLAGS)			\
	-DLOCALE_DIR=\""$(datadir)/locale"\"	\
	$(DISABLE_DEPRECATED_CFLAGS)

AM_CFLAGS = $(WARN_CFLAGS)

cafe_session_save_SOURCES =			\
	cafe-session-save.c

cafe_session_save_LDADD =			\
	$(SM_LIBS)				\
	$(ICE_LIBS)				\
	$(MATE_SESSION_LIBS)

cafe_session_inhibit_SOURCES =			\
	cafe-session-inhibit.c

cafe_session_inhibit_CPPFLAGS =			\
	$(AM_CPPFLAGS)				\
	$(MATE_SESSION_CFLAGS)			\
	-DLOCALE_DIR=\""$(datadir)/locale"\"	\
	$(DISABLE_DEPRECATED_CFLAGS)

cafe_session_inhibit_LDADD =			\
	$(MATE_SESSION_LIBS)

if HAVE_GLESV2
libexec_PROGRAMS += cafe-session-check-accelerated-gles-helper

cafe_session_check_accelerated_gles_helper_SOURCES =	\
	cafe-session-check-accelerated-common.h		\
	cafe-session-check-accelerated-gles-helper.c

cafe_session_check_accelerated_gles_helper_CPPFLAGS =	\
	-DPKGDATADIR=\""$(pkgdatadir)"\"		\
	$(AM_CPPFLAGS)					\
	$(GTK3_CFLAGS)					\
	$(GLES_TEST_CFLAGS)				\
	$(X11_CFLAGS)

cafe_session_check_accelerated_gles_helper_LDADD =	\
	$(GLES_TEST_LIBS)				\
	$(GTK3_LIBS)					\
	$(X11_LIBS)
endif

cafe_session_check_accelerated_gl_helper_SOURCES =	\
	cafe-session-check-accelerated-common.h		\
	cafe-session-check-accelerated-gl-helper.c

cafe_session_check_accelerated_gl_helper_CPPFLAGS =	\
	-DPKGDATADIR=\""$(pkgdatadir)"\"		\
	$(AM_CPPFLAGS)					\
	$(GL_TEST_CFLAGS)				\
	$(X11_CFLAGS)

cafe_session_check_accelerated_gl_helper_LDADD = 	\
	$(GL_TEST_LIBS)					\
	$(X11_LIBS)

cafe_session_check_accelerated_SOURCES =       	\
	cafe-session-check-accelerated-common.h	\
	cafe-session-check-accelerated.c

cafe_session_check_accelerated_CPPFLAGS =	\
	-DLIBEXECDIR=\""$(libexecdir)"\"	\
	$(AM_CPPFLAGS)				\
	$(GTK3_CFLAGS)				\
	$(GL_TEST_CFLAGS)			\
	$(X11_CPPFLAGS)

cafe_session_check_accelerated_LDADD =	\
	$(GTK3_LIBS)			\
	$(GL_TEST_LIBS)			\
	$(X11_LIBS)

-include $(top_srcdir)/git.mk
