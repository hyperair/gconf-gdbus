INCLUDES=-I$(top_srcdir) $(GLIB_CFLAGS) $(GMODULE_CFLAGS) $(ORBIT_CFLAGS)\
        -DGCONF_SRCDIR=\""$(absolute_top_srcdir)"\" -DGCONF_SYSCONFDIR=\""$(sysconfdir)"\" -DGCONF_BACKEND_DIR=\""$(pkglibdir)"\"

lib_LTLIBRARIES = libgconf-cxxclient.la

gconfincludedir = $(includedir)/gconf
gconfinclude_HEADERS =     \
	gconf-cxx.h

libgconf_cxxclient_la_SOURCES = \
	gconf-cxx.cc

libgconf_cxxclient_la_LIBADD = $(GLIB_LIBS) $(GMODULE_LIBS) $(ORBIT_LIBS) $(top_srcdir)/gconf/libgconf-client.la
