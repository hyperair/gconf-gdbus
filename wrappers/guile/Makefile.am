
gconfmoduledir = $(datadir)/guile/gconf

# -I$(builddir) catches the .x files
INCLUDES =  -I$(top_builddir)/wrappers/guile -I$(top_srcdir)/gconf -I$(includedir) $(GUILE_CFLAGS)  $(ORBIT_CFLAGS) $(GLIB_CFLAGS) \
	 -DG_LOG_DOMAIN=\"libgconf-scm\" -DGCONF_GUILE_DIR=\""$(gconfmoduledir)"\"

lib_LTLIBRARIES = libgconf-scm.la

libgconf_scm_la_SOURCES =	\
	scm-gconf.c

libgconfincludedir = $(includedir)/gconf

libgconfinclude_HEADERS = \
	scm-gconf.h

BUILT_SOURCES = $(guile_snarfs)

guile_snarfs = \
	scm-gconf.x

libgconf_scm_la_LDFLAGS = -version-info $(SCMGCONF_CURRENT):$(SCMGCONF_REVISION):$(SCMGCONF_AGE)

SUFFIXES=.x

%.x: %.c
	$(CC) -E -DSCM_MAGIC_SNARFER $(INCLUDES) $< | grep "^ *% *% *%" | sed -e "s/^ *% *% *%//" > $@


MAINTAINERCLEANFILES = $(guile_snarfs)
CLEANFILES = $(guile_snarfs)

EXTRA_DIST = $(guile_snarfs) scm-gconf-private.h ## guile_snarfs is a hack caused by an automake bug I think

### The hacked Guile binary because the Guile "module system" isn't quite there

bin_PROGRAMS=gconf-guile

gconf_guile_SOURCES=gconf-guile.c

gconf_guile_LDADD = $(GUILE_LIBS) $(ORBIT_LIBS) $(GLIB_LIBS) $(top_builddir)/gconf/libgconf-client.la libgconf-scm.la
