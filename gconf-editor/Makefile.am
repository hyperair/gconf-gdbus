INCLUDES=-I$(top_srcdir) \
	$(GNOME_CFLAGS) $(OAF_CFLAGS) \
         -DG_LOG_DOMAIN=\"GConfEd\" -DGNOMELOCALEDIR=\""$(datadir)/locale"\" \
	-I../intl -I$(top_srcdir)/intl -I$(top_srcdir)/gconf

bin_PROGRAMS = gconf-editor

gconf_editor_SOURCES = 	\
	app.h		\
	app.c		\
	gconf-editor.c 	\
	menus.h		\
	menus.c

gconf_editor_LDADD = $(GNOME_LIBS) $(INTLLIBS) $(ORBIT_LIBS) $(top_builddir)/gconf/libgconf.la
