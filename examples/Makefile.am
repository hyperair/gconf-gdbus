INCLUDES=-I$(top_srcdir) -I$(top_srcdir)/gconf -I$(top_builddir)/gconf  -I$(top_srcdir)/wrappers/gtk $(GLIB_CFLAGS) $(OAF_CFLAGS) $(GMODULE_CFLAGS) $(ORBIT_CFLAGS) $(GTK_CFLAGS)\
        -DGCONF_SRCDIR=\""$(absolute_top_srcdir)"\" -DGCONF_SYSCONFDIR=\""$(sysconfdir)"\" -DGCONF_BACKEND_DIR=\""$(pkglibdir)"\"

if GTK
GTK_EXAMPLES=basic-gconf-app
else
GTK_EXAMPLES=
endif

noinst_PROGRAMS=$(GTK_EXAMPLES)

basic_gconf_app_SOURCES = basic-gconf-app.c

basic_gconf_app_LDADD = $(ORBIT_LIBS) $(GTK_LIBS) $(top_builddir)/gconf/libgconf.la $(top_builddir)/wrappers/gtk/libgconf-gtk.la

