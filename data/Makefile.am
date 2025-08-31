SUBDIRS = icons

bin_SCRIPTS = cafe-wm

uidir = $(pkgdatadir)
ui_DATA = \
	session-properties.ui	\
	csm-inhibit-dialog.ui

hwcompatdir = $(pkgdatadir)
hwcompat_DATA = hardware-compatibility

xsessiondir = $(datadir)/xsessions
xsession_in_files = cafe.desktop.in
xsession_DATA = $(xsession_in_files:.desktop.in=.desktop)

desktopdir = $(datadir)/applications
desktop_in_files = cafe-session-properties.desktop.in cafe-wm.desktop.in
desktop_DATA = cafe-session-properties.desktop

if USE_CAFE_WM
desktop_DATA += cafe-wm.desktop
endif

SUFFIXES = .desktop.in .desktop
.desktop.in.desktop:
	$(AM_V_GEN) $(MSGFMT) --desktop --template $< -d $(top_srcdir)/po -o $@

gsettingsschema_in_files = org.cafe.session.gschema.xml.in
gsettings_SCHEMAS = $(gsettingsschema_in_files:.xml.in=.xml)
@GSETTINGS_RULES@

EXTRA_DIST =					\
	cafe-wm				\
	$(xsession_in_files)			\
	$(gsettingsschema_in_files)		\
	$(hwcompat_DATA)			\
	$(ui_DATA)				\
	$(desktop_in_files)			\
	$(pixmap_DATA_dist)

CLEANFILES =					\
	$(xsession_DATA)			\
	$(desktop_DATA)				\
	$(gsettings_SCHEMAS)

-include $(top_srcdir)/git.mk
