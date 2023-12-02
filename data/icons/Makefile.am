appiconsdir = $(datadir)/icons/hicolor

nobase_dist_appicons_DATA = \
	16x16/apps/cafe-session-properties.png \
	22x22/apps/cafe-session-properties.png \
	24x24/apps/cafe-session-properties.png \
	32x32/apps/cafe-session-properties.png \
	48x48/apps/cafe-session-properties.png \
	scalable/apps/cafe-session-properties.svg

noinst_DATA = \
	16x16/apps/cafe-session-properties.svg \
	22x22/apps/cafe-session-properties.svg \
	32x32/apps/cafe-session-properties.svg

ctk_update_icon_cache = ctk-update-icon-cache -f -t $(datadir)/icons/hicolor

install-data-hook: update-icon-cache
uninstall-hook: update-icon-cache
update-icon-cache:
	@-if test -z "$(DESTDIR)"; then \
		echo "Updating Ctk icon cache."; \
		$(ctk_update_icon_cache); \
	else \
		echo "*** Icon cache not updated.  After (un)install, run this:"; \
		echo "***   $(ctk_update_icon_cache)"; \
	fi

EXTRA_DIST = $(noinst_DATA)

-include $(top_srcdir)/git.mk
