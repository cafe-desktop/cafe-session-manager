XSLTPROC_FLAGS = \
	--nonet \
	--stringparam man.output.quietly 1 \
	--stringparam funcsynopsis.style ansi \
	--stringparam man.th.extra1.suppress 1 \
	--stringparam man.authors.section.enabled 0 \
	--stringparam man.copyright.section.enabled 0

.xml.1:
	$(AM_V_GEN) $(XSLTPROC) $(XSLTPROC_FLAGS) http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<

man_MANS =				\
	cafe-session.1			\
	cafe-session-properties.1	\
	cafe-session-save.1		\
	cafe-wm.1			\
	cafe-session-inhibit.1

EXTRA_DIST =				\
	cafe-session-inhibit.xml	\
	$(man_MANS)

CLEANFILES =				\
	cafe-session-inhibit.1

-include $(top_srcdir)/git.mk
