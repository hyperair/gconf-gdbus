INCLUDES=-I$(top_srcdir) \
	-I$(includedir) $(GNOME_INCLUDEDIR) \
         -DG_LOG_DOMAIN=\"GConfEd\" -DGNOMELOCALEDIR=\""$(datadir)/locale"\" \
	-I../intl -I$(top_srcdir)/intl

bin_PROGRAMS = gconf-editor

gconf_editor_SOURCES = \
	gconf-editor.c

gconf_editor_LDADD = $(GNOMEUI_LIBS) $(GNOME_LIBDIR) $(INTLLIBS)
