NULL=

INCLUDES=\
	-I$(top_srcdir) \
	-I$(top_builddir) \
	$(GLIB_CFLAGS) \
	$(ORBIT_CFLAGS) \
	$(XML_CFLAGS) \
	$(OAF_CFLAGS) \
        -DGCONF_SRCDIR=\""$(absolute_top_srcdir)"\" \
	-DGCONF_CONFDIR=\""$(sysconfdir)/gconf/$(MAJOR_VERSION)"\" \
	-DGCONF_BUILDDIR=\""$(top_builddir)"\" \
	-DGCONF_BACKEND_DIR=\""$(pkglibdir)/$(MAJOR_VERSION)"\" \
	-DVERSION=\""$(VERSION)"\" \
	-DIID=\""OAFIID:gconfd:19991118"\" \
	-DGCONF_ENABLE_INTERNALS=1 \
	$(NULL)

EFENCE=

lib_LTLIBRARIES = libgconf-1.la

bin_PROGRAMS = gconfd-1 gconftool-1

CORBA_SOURCES = GConf-common.c GConf-skels.c GConf-stubs.c GConf.h

BUILT_SOURCES = $(CORBA_SOURCES)

gconfincludedir = $(includedir)/gconf/$(MAJOR_VERSION)/gconf
gconfinclude_HEADERS =     \
	gconf.h            \
	gconf-backend.h		\
	gconf-changeset.h	\
	gconf-glib-public.h	\
	gconf-listeners.h  \
	gconf-schema.h 	   \
	gconf-value.h	   \
	gconf-error.h	   \
	gconf-engine.h

## This is broken I know, I'm going to break the server/client up and have a library later

gconfd_1_SOURCES = \
	gconf-database.h	\
	gconf-database.c	\
	gconf-sources.h		\
	gconfd.h		\
	gconfd.c

gconfd_1_LDADD = $(EFENCE) $(OAF_LIBS) $(GLIB_LIBS) libgconf-$(MAJOR_VERSION).la

# gconf_testclient_SOURCES = \
# 	testclient.c

# gconf_testclient_LDADD = $(GLIB_LIBS) $(ORBIT_LIBS) libgconf-client.la

gconftool_1_SOURCES = \
	gconftool.c

gconftool_1_LDADD = $(EFENCE) $(OAF_LIBS) $(GLIB_LIBS) $(ORBIT_LIBS) $(POPT_LIBS) $(XML_LIBS) libgconf-$(MAJOR_VERSION).la

$(CORBA_SOURCES): $(srcdir)/GConf.idl
	orbit-idl $(srcdir)/GConf.idl

libgconf_1_la_SOURCES = \
	gconf-internals.c	\
	gconf-internals.h	\
	gconf-backend.c		\
	gconf-changeset.c	\
	gconf-error.c		\
	gconf-glib.c		\
	gconf-glib-private.h	\
	gconf-listeners.c	\
	gconf-locale.h  	\
	gconf-locale.c  	\
	gconf-schema.c		\
	gconf-sources.c		\
	gconf-value.c		\
	gconf.c			\
	$(CORBA_SOURCES)

libgconf_1_la_LDFLAGS = -version-info $(GCONF_CURRENT):$(GCONF_REVISION):$(GCONF_AGE)

libgconf_1_la_LIBADD =  $(OAF_LIBS) $(GLIB_LIBS)

EXTRA_DIST=GConf.idl gconfd.oafinfo.in default.path.in

install-exec-local:
	(cd $(DESTDIR)$(bindir) && $(LN_S) -f gconftool-$(MAJOR_VERSION) gconftool)

install-data-local:
	-mkdir -p $(DESTDIR)$(sysconfdir)/gconf/$(MAJOR_VERSION)
	$(INSTALL_DATA) default.path $(DESTDIR)$(sysconfdir)/gconf/$(MAJOR_VERSION)/path.example
	-mkdir -p $(DESTDIR)$(datadir)/oaf
	$(INSTALL_DATA) gconfd.oafinfo $(DESTDIR)$(datadir)/oaf/gconfd-$(MAJOR_VERSION).oafinfo

