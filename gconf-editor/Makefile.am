INCLUDES=-I$(top_srcdir) \
	-I$(includedir) $(GNOME_INCLUDEDIR) \
         -DG_LOG_DOMAIN=\"GConfEd\" -DGNOMELOCALEDIR=\""$(datadir)/locale"\" \
	-I../intl -I$(top_srcdir)/intl -I$(top_srcdir)/gconf

bin_PROGRAMS = gconf-editor

gconf_editor_SOURCES = 	\
	app.h		\
	app.c		\
	gconf-editor.c 	\
	menus.h		\
	menus.c

gconf_editor_LDADD = $(GNOMEUI_LIBS) $(GNOME_LIBDIR) $(INTLLIBS) $(ORBIT_LIBS) $(top_srcdir)/gconf/libgconf-client.la
