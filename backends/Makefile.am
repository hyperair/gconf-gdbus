INCLUDES= -I$(top_srcdir) $(GLIB_CFLAGS) $(ORBIT_CFLAGS) $(XML_CFLAGS)

backenddir = $(libdir)/gconf

backend_LTLIBRARIES = libgconfbackend-xml.la

libgconfbackend_xml_la_SOURCES = \
	xml-backend.c

libgconfbackend_xml_la_LDFLAGS = -avoid-version -module
libgconfbackend_xml_la_LIBADD  = $(GLIB_LIBS) $(XML_LIBS)
