INCLUDES=-I$(top_srcdir) $(GLIB_CFLAGS) $(ORBIT_CFLAGS) \
        -DGCONF_SRCDIR=\""$(absolute_top_srcdir)"\" -DGCONF_SYSCONFDIR=\""$(sysconfdir)"\" -DGCONF_BACKEND_DIR=\""$(libdir)/gconf"\"

lib_LTLIBRARIES = libgconf-client.la

bin_PROGRAMS = gconfd gconftool

CORBA_SOURCES = GConf-common.c GConf-skels.c GConf-stubs.c GConf.h

BUILT_SOURCES = $(CORBA_SOURCES)

gconfincludedir = $(includedir)/gconf
gconfinclude_HEADERS =     \
        GConf.h            \
	gconf.h            \
	gconf-orbit.h      \
	gconf-schema.h 	   \
	gconf-value.h	   \
	gconf-error.h	   \
	gconf-engine.h	\
	gconf-sources.h

## This is broken I know, I'm going to break the server/client up and have a library later

gconfd_SOURCES = \
	gconfd.c	\
	gconfd-error.h	\
	gconfd-error.c  \
	gconf-sources.c

gconfd_LDADD = $(GLIB_LIBS) $(ORBIT_LIBS) libgconf-client.la

# gconf_testclient_SOURCES = \
# 	testclient.c

# gconf_testclient_LDADD = $(GLIB_LIBS) $(ORBIT_LIBS) libgconf-client.la

gconftool_SOURCES = \
	gconftool.c

gconftool_LDADD = $(GLIB_LIBS) $(ORBIT_LIBS) $(POPT_LIBS) libgconf-client.la

$(CORBA_SOURCES): GConf.idl
	orbit-idl GConf.idl

libgconf_client_la_SOURCES = \
	gconf-orbit.c		\
	gconf-orbit.h		\
	gconf-internals.c	\
	gconf-internals.h	\
	gconf-backend.h		\
	gconf-backend.c		\
	gconf-error.h		\
	gconf-error.c		\
	gconf-schema.h		\
	gconf-schema.c		\
	gconf-value.c		\
	gconf.h			\
	gconf.c			\
	$(CORBA_SOURCES)

libgconf_client_la_LDFLAGS = -version-info $(GCONF_CURRENT):$(GCONF_REVISION):$(GCONF_AGE)
libgconf_client_la_LIBADD = $(GLIB_LIBS) $(ORBIT_LIBS)

EXTRA_DIST=GConf.idl

## This is totally broken, I just don't feel like doing the configure.in work to 
## detect Gnome, etc. Depends on the non-GUI testclient to suck in its dependencies.
gnome-testclient: gnome-testclient.c gconf-testclient
	 cc -I.. `gnome-config --libs gnomeui gnorba` `gnome-config --cflags gnomeui gnorba` -Wall -ggdb -O2 gnome-testclient.c libgconf-client.a -o gnome-testclient

