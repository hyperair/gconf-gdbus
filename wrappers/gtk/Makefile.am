INCLUDES=-I$(top_srcdir) -I$(top_builddir) -I$(top_builddir)/gconf \
	 -DGCONF_LOCALE_DIR=\""$(gconflocaledir)"\" $(GLIB_CFLAGS) $(OAF_CFLAGS) $(GMODULE_CFLAGS) $(ORBIT_CFLAGS) $(GTK_CFLAGS) \
	-DGCONF_SRCDIR=\""$(absolute_top_srcdir)"\" -DGCONF_SYSCONFDIR=\""$(sysconfdir)/$(MAJOR_VERSION)"\" -DGCONF_BACKEND_DIR=\""$(pkglibdir)/$(MAJOR_VERSION)"\"  -DGCONF_ENABLE_INTERNALS=1 -DG_LOG_DOMAIN=\"GConfClient\"

lib_LTLIBRARIES = libgconf-gtk-1.la

gconfincludedir = $(includedir)/gconf/$(MAJOR_VERSION)/gconf
gconfinclude_HEADERS =     \
	gconf-client.h

libgconf_gtk_1_la_SOURCES = \
	gconf-client.c

libgconf_gtk_1_la_LDFLAGS=-version-info $(GCONFGTK_CURRENT):$(GCONFGTK_REVISION):$(GCONFGTK_AGE)

libgconf_gtk_1_la_LIBADD= $(INTLLIBS) $(OAF_LIBS) $(GTK_LIBS)

noinst_PROGRAMS= testgconfclient

testgconfclient_SOURCES = testgconfclient.c

testgconfclient_DEPS=$(top_builddir)/gconf/libgconf-$(MAJOR_VERSION).la libgconf-gtk-$(MAJOR_VERSION).la
testgconfclient_LDADD = $(INTLLIBS) $(ORBIT_LIBS) $(GTK_LIBS) $(top_builddir)/gconf/libgconf-$(MAJOR_VERSION).la $(top_builddir)/wrappers/gtk/libgconf-gtk-$(MAJOR_VERSION).la
