INCLUDES=-I$(top_srcdir) $(GLIB_CFLAGS) $(GMODULE_CFLAGS) $(ORBIT_CFLAGS) $(GTK_CFLAGS)\
        -DGCONF_SRCDIR=\""$(absolute_top_srcdir)"\" -DGCONF_SYSCONFDIR=\""$(sysconfdir)"\" -DGCONF_BACKEND_DIR=\""$(pkglibdir)"\"

lib_LTLIBRARIES = libgconf-gtkclient.la

gconfincludedir = $(includedir)/gconf
gconfinclude_HEADERS =     \
	gconf-gtk.h


libgconf_gtkclient_la_SOURCES = \
	gconf-gtk.c

libgconf_gtkclient_la_LIBADD = $(ORBIT_LIBS) $(GTK_LIBS) $(top_srcdir)/gconf/libgconf-client.la
