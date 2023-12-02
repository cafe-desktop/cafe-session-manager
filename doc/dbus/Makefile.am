SPEC_XML_FILES =					\
	cafe-session.xml				\
	org.cafe.SessionManager.ref.xml		\
	org.cafe.SessionManager.Client.ref.xml		\
	org.cafe.SessionManager.ClientPrivate.ref.xml	\
	org.cafe.SessionManager.Inhibitor.ref.xml	\
	org.cafe.SessionManager.Presence.ref.xml

if DOCBOOK_DOCS_ENABLED

htmldocdir = $(docdir)/dbus
htmldoc_DATA = cafe-session.html

cafe-session.html: $(SPEC_XML_FILES)
	$(AM_V_GEN)$(XMLTO) xhtml-nochunks -m $(top_srcdir)/doc/dbus/config.xsl cafe-session.xml

endif # DOCBOOK_DOCS_ENABLED

org.cafe.SessionManager.ref.xml: $(top_srcdir)/cafe-session/org.cafe.SessionManager.xml spec-to-docbook.xsl
	$(AM_V_GEN)$(XSLTPROC) $(top_srcdir)/doc/dbus/spec-to-docbook.xsl $< | tail -n +2 > $@
org.cafe.SessionManager.Client.ref.xml: $(top_srcdir)/cafe-session/org.cafe.SessionManager.Client.xml spec-to-docbook.xsl
	$(AM_V_GEN)$(XSLTPROC) $(top_srcdir)/doc/dbus/spec-to-docbook.xsl $< | tail -n +2 > $@
org.cafe.SessionManager.ClientPrivate.ref.xml: $(top_srcdir)/cafe-session/org.cafe.SessionManager.ClientPrivate.xml spec-to-docbook.xsl
	$(AM_V_GEN)$(XSLTPROC) $(top_srcdir)/doc/dbus/spec-to-docbook.xsl $< | tail -n +2 > $@
org.cafe.SessionManager.Inhibitor.ref.xml: $(top_srcdir)/cafe-session/org.cafe.SessionManager.Inhibitor.xml spec-to-docbook.xsl
	$(AM_V_GEN)$(XSLTPROC) $(top_srcdir)/doc/dbus/spec-to-docbook.xsl $< | tail -n +2 > $@
org.cafe.SessionManager.Presence.ref.xml: $(top_srcdir)/cafe-session/org.cafe.SessionManager.Presence.xml spec-to-docbook.xsl
	$(AM_V_GEN)$(XSLTPROC) $(top_srcdir)/doc/dbus/spec-to-docbook.xsl $< | tail -n +2 > $@

BUILT_SOURCES =	\
	org.cafe.SessionManager.ref.xml \
	org.cafe.SessionManager.Client.ref.xml \
	org.cafe.SessionManager.ClientPrivate.ref.xml \
	org.cafe.SessionManager.Inhibitor.ref.xml \
	org.cafe.SessionManager.Presence.ref.xml

CLEANFILES =				\
	$(BUILT_SOURCES)		\
	$(htmldoc_DATA)

EXTRA_DIST =				\
	cafe-session.xml.in		\
	config.xsl			\
	docbook.css			\
	dbus-introspect-docs.dtd	\
	spec-to-docbook.xsl

-include $(top_srcdir)/git.mk
