INCLUDES=-I$(top_srcdir) $(GLIB_CFLAGS) $(GMODULE_CFLAGS)

bin_PROGRAMS = gconfd

gconfd_SOURCES = \
	gconf-internals.c	\
	gconf-internals.h	\
	gconf-backend.h		\
	gconf-backend.c		\
	gconfd.c

gconfd_LDADD = $(GLIB_LIBS) $(GMODULE_LIBS)
