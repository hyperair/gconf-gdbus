INCLUDES=-I$(top_srcdir) -I$(top_srcdir)/gconf -I$(top_builddir)/gconf \
	-I$(top_srcdir)/wrappers/gtk -I$(top_builddir)/wrappers/gtk \
	$(GLIB_CFLAGS) $(OAF_CFLAGS) $(GMODULE_CFLAGS) $(ORBIT_CFLAGS) $(GTK_CFLAGS) \
        -DGCONF_SRCDIR=\""$(absolute_top_srcdir)"\" -DGCONF_SYSCONFDIR=\""$(sysconfdir)"\"

EFENCE=

if GTK
GTK_EXAMPLES=basic-gconf-app simple-view simple-controller
else
GTK_EXAMPLES=
endif

noinst_PROGRAMS=$(GTK_EXAMPLES)

GTK_EXAMPLES_LINK= $(EFENCE) $(INTLLIBS) $(ORBIT_LIBS) $(GTK_LIBS) $(top_builddir)/gconf/libgconf-$(MAJOR_VERSION).la $(top_builddir)/wrappers/gtk/libgconf-gtk-$(MAJOR_VERSION).la $(EFENCE)

basic_gconf_app_SOURCES = basic-gconf-app.c

basic_gconf_app_LDADD = $(GTK_EXAMPLES_LINK)

simple_view_SOURCES = simple-view.c

simple_view_LDADD = $(GTK_EXAMPLES_LINK)

simple_controller_SOURCES = simple-controller.c

simple_controller_LDADD = $(GTK_EXAMPLES_LINK)



