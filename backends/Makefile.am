INCLUDES= -I$(top_srcdir) -I$(top_builddir) -I$(top_builddir)/gconf \
	$(GLIB_CFLAGS) $(OAF_CFLAGS) $(ORBIT_CFLAGS) $(XML_CFLAGS) \
	-DGCONF_ENABLE_INTERNALS=1 -DG_LOG_DOMAIN=\"GConf-Backends\"

EXTRA_DIST=README.bdb

backenddir = $(pkglibdir)/$(MAJOR_VERSION)

if BDB
BDBBACKEND=libgconfbackend-bdb.la
else
BDBBACKEND=
endif

backend_LTLIBRARIES = libgconfbackend-xml.la $(BDBBACKEND)

libgconfbackend_xml_la_SOURCES = \
	xml-cache.h		\
	xml-cache.c		\
	xml-dir.h		\
	xml-dir.c		\
	xml-entry.h		\
	xml-entry.c		\
	xml-backend.c

libgconfbackend_xml_la_LDFLAGS = -avoid-version -module
libgconfbackend_xml_la_LIBADD  = $(GLIB_LIBS) $(XML_LIBS)

libgconfbackend_bdb_la_SOURCES = bdb.c bdb.h bdb-backend.c val-encode.c val-encode.h dir-utils.c dir-utils.h


libgconfbackend_bdb_la_LDFLAGS = -avoid-version -module
libgconfbackend_bdb_la_LIBADD = $(GLIB_LIBS) $(BDB_LIBS)
