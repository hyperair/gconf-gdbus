INCLUDES=-I$(top_srcdir) $(GLIB_CFLAGS) $(ORBIT_CFLAGS) $(XML_CFLAGS) $(OAF_CFLAGS) \
        -DGCONF_SRCDIR=\""$(absolute_top_srcdir)"\" -DGCONF_SYSCONFDIR=\""$(sysconfdir)"\" \
	-DGCONF_BACKEND_DIR=\""$(libdir)/gconf"\" -DVERSION=\""$(VERSION)"\" -DIID=\""OAFIID:gconfd:19991118"\"

EFENCE=

lib_LTLIBRARIES = libgconf.la

bin_PROGRAMS = gconfd gconftool

CORBA_SOURCES = GConf-common.c GConf-skels.c GConf-stubs.c GConf.h

BUILT_SOURCES = $(CORBA_SOURCES)

gconfincludedir = $(includedir)/gconf
gconfinclude_HEADERS =     \
	gconf.h            \
	gconf-backend.h		\
	gconf-changeset.h	\
	gconf-listeners.h  \
	gconf-schema.h 	   \
	gconf-value.h	   \
	gconf-error.h	   \
	gconf-engine.h	\
	gconf-sources.h

## This is broken I know, I'm going to break the server/client up and have a library later

gconfd_SOURCES = \
	gconf-locale.h  	\
	gconf-locale.c  	\
	gconfd.c	\
	gconf-sources.c

gconfd_LDADD = $(EFENCE) $(OAF_LIBS) $(GLIB_LIBS) libgconf.la

# gconf_testclient_SOURCES = \
# 	testclient.c

# gconf_testclient_LDADD = $(GLIB_LIBS) $(ORBIT_LIBS) libgconf-client.la

gconftool_SOURCES = \
	gconftool.c

gconftool_LDADD = $(EFENCE) $(OAF_LIBS) $(GLIB_LIBS) $(ORBIT_LIBS) $(POPT_LIBS) $(XML_LIBS) libgconf.la

$(CORBA_SOURCES): $(srcdir)/GConf.idl
	orbit-idl $(srcdir)/GConf.idl

libgconf_la_SOURCES = \
	gconf-internals.c	\
	gconf-internals.h	\
	gconf-backend.c		\
	gconf-changeset.c	\
	gconf-error.c		\
	gconf-listeners.c	\
	gconf-schema.c		\
	gconf-value.c		\
	gconf.c			\
	$(CORBA_SOURCES)

libgconf_la_LDFLAGS = -version-info $(GCONF_CURRENT):$(GCONF_REVISION):$(GCONF_AGE) $(OAF_LIBS)

EXTRA_DIST=GConf.idl gconfd.oafinfo

oafinfodir=$(datadir)/oaf
oafinfo_DATA=gconfd.oafinfo
