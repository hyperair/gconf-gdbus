INCLUDES= -I$(top_srcdir) -I$(top_builddir) -I$(top_builddir)/gconf \
	$(DEPENDENT_WITH_XML_CFLAGS) \
	-DGCONF_ENABLE_INTERNALS=1 -DG_LOG_DOMAIN=\"GConf-Backends\"

backenddir = $(pkglibdir)/$(MAJOR_VERSION)

backend_LTLIBRARIES = libgconfbackend-xml.la
noinst_LTLIBRARIES = libgconfbackend-markup.la

libgconfbackend_xml_la_SOURCES = \
	xml-cache.h		\
	xml-cache.c		\
	xml-dir.h		\
	xml-dir.c		\
	xml-entry.h		\
	xml-entry.c		\
	xml-backend.c

libgconfbackend_xml_la_LDFLAGS = -avoid-version -module
libgconfbackend_xml_la_LIBADD  = $(DEPENDENT_WITH_XML_LIBS)

libgconfbackend_markup_la_SOURCES = 	\
	markup-backend.c		\
	markup-tree.h			\
	markup-tree.c

libgconfbackend_markup_la_LDFLAGS = -avoid-version -module
libgconfbackend_markup_la_LIBADD  = $(DEPENDENT_LIBS)

noinst_PROGRAMS = xml-test

xml_test_SOURCES= $(libgconfbackend_xml_la_SOURCES) xml-test.c
xml_test_LDADD = $(DEPENDENT_WITH_XML_LIBS) $(top_builddir)/gconf/libgconf-$(MAJOR_VERSION).la
