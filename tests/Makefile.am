
EFENCE=

INCLUDES = -I$(top_srcdir) -I$(top_builddir) -I$(includedir)  $(ORBIT_CFLAGS) $(GLIB_CFLAGS) \
	 -DG_LOG_DOMAIN=\"GConf-Tests\" -DGCONF_ENABLE_INTERNALS=1

noinst_PROGRAMS=testgconf testlisteners testschemas testchangeset testencode

testgconf_SOURCES=testgconf.c

testgconf_LDADD = $(ORBIT_LIBS) $(GLIB_LIBS) $(top_builddir)/gconf/libgconf.la  $(EFENCE)

testlisteners_SOURCES=testlisteners.c

testlisteners_LDADD = $(ORBIT_LIBS) $(GLIB_LIBS) $(top_builddir)/gconf/libgconf.la  $(EFENCE)

testschemas_SOURCES=testschemas.c

testschemas_LDADD = $(ORBIT_LIBS) $(GLIB_LIBS) $(top_builddir)/gconf/libgconf.la  $(EFENCE)

testchangeset_SOURCES=testchangeset.c

testchangeset_LDADD = $(ORBIT_LIBS) $(GLIB_LIBS) $(top_builddir)/gconf/libgconf.la  $(EFENCE)

testencode_SOURCES=testencode.c

testencode_LDADD = $(ORBIT_LIBS) $(GLIB_LIBS) $(top_builddir)/gconf/libgconf.la  $(EFENCE)
