NULL=

INCLUDES=\
	-I$(top_srcdir) \
	-I$(top_builddir) \
	$(GLIB_CFLAGS) \
	$(XML_CFLAGS) \
	$(OAF_CFLAGS) \
	$(GTK_CFLAGS) \
	-DG_LOG_DOMAIN=\"GConf\"                   \
	-DGCONF_LOCALE_DIR=\""$(gconflocaledir)"\"  \
        -DGCONF_SRCDIR=\""$(absolute_top_srcdir)"\" \
	-DGCONF_CONFDIR=\""$(sysconfdir)/gconf/$(MAJOR_VERSION)"\" \
	-DGCONF_BUILDDIR=\""$(top_builddir)"\" 			\
	-DGCONF_BACKEND_DIR=\""$(pkglibdir)/$(MAJOR_VERSION)"\" \
	-DVERSION=\""$(VERSION)"\" \
	-DIID=\""OAFIID:gconfd:19991118"\" \
	-DGCONF_ENABLE_INTERNALS=1 \
	-Wno-unused \
	$(NULL)

EFENCE=

lib_LTLIBRARIES = libgconf-2.la

bin_PROGRAMS = gconfd-2 gconftool-2

if GTK
GTK_PROGS = testgconfclient
else
GTK_PROGS =
endif

noinst_PROGRAMS = $(GTK_PROGS)

CORBA_SOURCES = GConf-common.c GConf-skels.c GConf-stubs.c GConf.h

BUILT_SOURCES = \
	$(CORBA_SOURCES)	\
	gconfmarshal.h		\
	gconfmarshal.c

CLEANFILES = \
	$(BUILT_SOURCES)	\
	stamp-gconfmarshal.h

dist-hook:
	cd $(distdir) ; rm -f $(CLEANFILES)

gconfincludedir = $(includedir)/gconf/$(MAJOR_VERSION)/gconf
gconfinclude_HEADERS = \
	gconf.h			\
	gconf-changeset.h	\
	gconf-listeners.h	\
	gconf-schema.h		\
	gconf-value.h		\
	gconf-error.h		\
	gconf-engine.h		\
	gconf-client.h

## This is broken I know, I'm going to break the server/client up and have a library later

gconfd_2_SOURCES = \
	gconf-database.h	\
	gconf-database.c	\
	gconf-sources.h		\
	gconfd.h		\
	gconfd.c

gconfd_2_LDADD = $(EFENCE) $(INTLLIBS) $(OAF_LIBS) $(GLIB_LIBS) libgconf-$(MAJOR_VERSION).la

# gconf_testclient_SOURCES = \
# 	testclient.c

# gconf_testclient_LDADD = $(GLIB_LIBS) $(OAF_LIBS) libgconf-client.la

gconftool_2_SOURCES = \
	gconftool.c

gconftool_2_LDADD = $(EFENCE) $(INTLLIBS) $(OAF_LIBS) $(GLIB_LIBS) $(POPT_LIBS) $(XML_LIBS) libgconf-$(MAJOR_VERSION).la

$(CORBA_SOURCES): $(srcdir)/GConf.idl
	orbit-idl $(srcdir)/GConf.idl

libgconf_2_la_SOURCES = \
	gconf-internals.c	\
	gconf-internals.h	\
	gconf-backend.h		\
	gconf-backend.c		\
	gconf-changeset.c	\
	gconf-error.c		\
	gconf-listeners.c	\
	gconf-locale.h  	\
	gconf-locale.c  	\
	gconf-schema.c		\
	gconf-sources.c		\
	gconf-value.c		\
	gconf.c			\
	gconf-client.c		\
	$(CORBA_SOURCES)

libgconf_2_la_LDFLAGS = -version-info $(GCONF_CURRENT):$(GCONF_REVISION):$(GCONF_AGE)

libgconf_2_la_LIBADD = $(INTLLIBS) $(OAF_LIBS) $(GLIB_LIBS)

EXTRA_DIST=GConf.idl gconfd.oafinfo.in default.path.in gconfmarshal.list

install-exec-local:
	(cd $(DESTDIR)$(bindir) && $(RM) gconftool && $(LN_S) -f gconftool-$(MAJOR_VERSION) gconftool)

install-data-local:
	-mkdir -p $(DESTDIR)$(sysconfdir)/gconf/$(MAJOR_VERSION)
	$(INSTALL_DATA) default.path $(DESTDIR)$(sysconfdir)/gconf/$(MAJOR_VERSION)/path.example
	-mkdir -p $(DESTDIR)$(datadir)/oaf
	$(INSTALL_DATA) gconfd.oafinfo $(DESTDIR)$(datadir)/oaf/gconfd-$(MAJOR_VERSION).oafinfo

stamp-gconfmarshal.h: @REBUILD@ gconfmarshal.list
	cd $(srcdir) \
	&& glib-genmarshal --prefix=gconf_marshal gconfmarshal.list --header >> xgen-gmh \
	&& (cmp -s xgen-gmh gconfmarshal.h || cp xgen-gmh gconfmarshal.h) \
	&& rm -f xgen-gmh xgen-gmh~ \
	&& echo timestamp > $(@F)

gconfmarshal.h gconfmarshal.c: @REBUILD@ stamp-gconfmarshal.h
	cd $(srcdir) \
	&& glib-genmarshal --prefix=gconf_marshal gconfmarshal.list --body >> xgen-gmc \
	&& cp xgen-gmc gconfmarshal.c \
	&& rm -f xgen-gmc xgen-gmc~

testgconfclient_SOURCES = \
	testgconfclient.c

testgconfclient_LDADD = \
	libgconf-2.la $(GTK_LIBS)
