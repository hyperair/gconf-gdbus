INCLUDES=-I$(top_srcdir) $(GLIB_CFLAGS) $(GMODULE_CFLAGS) $(ORBIT_CFLAGS) \
        -DGCONF_SRCDIR=\""$(absolute_top_srcdir)"\" -DGCONF_SYSCONFDIR=\""$(sysconfdir)"\" -DGCONF_BACKEND_DIR=\""$(pkglibdir)"\"

bin_PROGRAMS = gconfd gconf-testclient gconftool

CORBA_SOURCES = GConf-common.c GConf-skels.c GConf-stubs.c GConf.h

BUILT_SOURCES = $(CORBA_SOURCES)

## This is broken I know, I'm going to break the server/client up and have a library later

gconfd_SOURCES = \
	gconf-orbit.c		\
	gconf-orbit.h		\
	gconf-internals.c	\
	gconf-internals.h	\
	gconf-backend.h		\
	gconf-backend.c		\
	gconf.h			\
	gconf.c			\
	gconfd.c		\
	$(CORBA_SOURCES)

gconfd_LDADD = $(GLIB_LIBS) $(GMODULE_LIBS) $(ORBIT_LIBS)

gconf_testclient_SOURCES = \
	gconf-orbit.c		\
	gconf-orbit.h		\
	gconf-internals.c	\
	gconf-internals.h	\
	gconf-backend.h		\
	gconf-backend.c		\
	gconf.h			\
	gconf.c			\
	testclient.c		\
	$(CORBA_SOURCES)

gconf_testclient_LDADD = $(GLIB_LIBS) $(GMODULE_LIBS) $(ORBIT_LIBS)

gconftool_SOURCES = \
	gconf-orbit.c		\
	gconf-orbit.h		\
	gconf-internals.c	\
	gconf-internals.h	\
	gconf-backend.h		\
	gconf-backend.c		\
	gconf.h			\
	gconf.c			\
	gconftool.c		\
	$(CORBA_SOURCES)

gconftool_LDADD = $(GLIB_LIBS) $(GMODULE_LIBS) $(ORBIT_LIBS) -lpopt

$(CORBA_SOURCES): GConf.idl
	orbit-idl GConf.idl

## This is totally broken, I just don't feel like doing the configure.in work to 
## detect Gnome, etc. Depends on the non-GUI testclient to suck in its dependencies.
gnome-testclient: gnome-testclient.c gconf-testclient
	 cc -I.. `gnome-config --libs gnomeui gnorba` `gnome-config --cflags gnomeui gnorba` -Wall -ggdb -O2 gnome-testclient.c gconf.o gconf-internals.o gconf-orbit.o gconf-backend.o GConf-common.o GConf-stubs.o GConf-skels.o -o gnome-testclient

