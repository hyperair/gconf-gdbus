INCLUDES=-I$(top_srcdir) -I$(top_srcdir)/gconf -I$(top_builddir)/gconf $(GLIB_CFLAGS) $(OAF_CFLAGS) $(GMODULE_CFLAGS) $(ORBIT_CFLAGS) $(GNOME_CFLAGS)\
        -DGCONF_SRCDIR=\""$(absolute_top_srcdir)"\" -DGCONF_SYSCONFDIR=\""$(sysconfdir)"\" -DGCONF_BACKEND_DIR=\""$(pkglibdir)"\"

lib_LTLIBRARIES = libgconf-gtk.la

gconfincludedir = $(includedir)/gconf
gconfinclude_HEADERS =     \
	gconf-client.h

libgconf_gtk_la_SOURCES = \
	gconf-client.c

libgconf_gtk_la_LDFLAGS=-version-info $(GCONFGTK_CURRENT):$(GCONFGTK_REVISION):$(GCONFGTK_AGE)

libgconf_gtk_la_LIBADD=$(OAF_LIBS) $(GNOME_LIBS)

noinst_PROGRAMS= testgconfclient

testgconfclient_SOURCES = testgconfclient.c

testgconfclient_LDADD = $(ORBIT_LIBS) $(GNOME_LIBS) $(top_builddir)/gconf/libgconf.la $(top_builddir)/wrappers/gtk/libgconf-gtk.la

