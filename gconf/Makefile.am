INCLUDES=-I$(top_srcdir) $(GLIB_CFLAGS) $(GMODULE_CFLAGS) $(ORBIT_CFLAGS)

bin_PROGRAMS = gconfd gconf-testclient

CORBA_SOURCES = GConf-common.c GConf-skels.c GConf-stubs.c GConf.h

BUILT_SOURCES = $(CORBA_SOURCES)

## This is broken I know, I'm going to break the server/client up and have a library later

gconfd_SOURCES = \
	gconf-internals.c	\
	gconf-internals.h	\
	gconf-backend.h		\
	gconf-backend.c		\
	gconf.h			\
	gconfd.c		\
	$(CORBA_SOURCES)

gconfd_LDADD = $(GLIB_LIBS) $(GMODULE_LIBS) $(ORBIT_LIBS)

gconf_testclient_SOURCES = \
	gconf-internals.c	\
	gconf-internals.h	\
	gconf-backend.h		\
	gconf-backend.c		\
	gconf.h			\
	gconf.c			\
	testclient.c		\
	$(CORBA_SOURCES)

gconf_testclient_LDADD = $(GLIB_LIBS) $(GMODULE_LIBS) $(ORBIT_LIBS)

$(CORBA_SOURCES): GConf.idl
	orbit-idl GConf.idl
