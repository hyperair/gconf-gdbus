
INCLUDES = -I$(top_srcdir)/gconf -I$(includedir)  $(ORBIT_CFLAGS) $(GLIB_CFLAGS) \
	 -DG_LOG_DOMAIN=\"testgconf\"

bin_PROGRAMS=testgconf

testgconf_SOURCES=testgconf.c

testgconf_LDADD = $(ORBIT_LIBS) $(GLIB_LIBS) $(top_builddir)/gconf/libgconf-client.la
