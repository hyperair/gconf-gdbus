NULL=

GCONFD_BINARY_NAME=gconfd-2

INCLUDES=								\
	-I$(top_srcdir)							\
	-I$(top_builddir)						\
	$(DEPENDENT_WITH_XML_CFLAGS)					\
	-DG_LOG_DOMAIN=\"GConf\"					\
	-DGCONF_LOCALE_DIR=\""$(gconflocaledir)"\"			\
        -DGCONF_SRCDIR=\""$(absolute_top_srcdir)"\"			\
	-DGCONF_CONFDIR=\""$(sysconfdir)/gconf/$(MAJOR_VERSION)"\"	\
	-DGCONF_ETCDIR=\""$(sysconfdir)/gconf"\"			\
	-DGCONF_BINDIR=\""$(bindir)"\"					\
	-DGCONF_BUILDDIR=\""$(top_builddir)"\"				\
	-DGCONF_BACKEND_DIR=\""$(pkglibdir)/$(MAJOR_VERSION)"\"		\
	-DVERSION=\""$(VERSION)"\"					\
	-DGCONF_ENABLE_INTERNALS=1					\
	-DGCONFD=\""$(GCONFD_BINARY_NAME)"\"				\
	$(NULL)

EFENCE=

lib_LTLIBRARIES = libgconf-2.la

bin_PROGRAMS = gconfd-2 gconftool-2

CORBA_SOURCES = GConf-common.c GConf-skels.c GConf-stubs.c GConf.h

## not BUILT_SOURCES which seems to make a mess. fix with automake 1.5?
built_sourcecode = 		\
	$(CORBA_SOURCES)	\
	gconf-enum-types.h	\
	gconf-enum-types.c	\
	gconfmarshal.h		\
	gconfmarshal.c

$(OBJECTS) $(libgconf_2_la_OBJECTS) $(gconftool_2_OBJECTS) $(gconfd_2_OBJECTS): $(built_sourcecode)

CLEANFILES = \
	$(built_sourcecode)	\
	stamp-gconfmarshal.h	\
	stamp-gconfmarshal.c	\
	s-enum-types-h		\
	s-enum-types-c		\
	stamp-gconfmarshal.h    \
	stamp-gconfmarshal.c

dist-hook:
	cd $(distdir) ; rm -f $(CLEANFILES)

gconf_headers = 		\
	gconf.h			\
	gconf-changeset.h	\
	gconf-listeners.h	\
	gconf-schema.h		\
	gconf-value.h		\
	gconf-error.h		\
	gconf-engine.h		\
	gconf-client.h

gconfincludedir = $(includedir)/gconf/$(MAJOR_VERSION)/gconf
gconfinclude_HEADERS = 		\
	$(gconf_headers) 	\
	gconf-enum-types.h

## This is broken I know, I'm going to break the server/client up and have a library later

gconfd_2_SOURCES = \
	gconf-database.h	\
	gconf-database.c	\
	gconf-sources.h		\
	gconfd.h		\
	gconfd.c

gconfd_2_LDADD = $(EFENCE) $(INTLLIBS) $(DEPENDENT_LIBS) libgconf-$(MAJOR_VERSION).la

# gconf_testclient_SOURCES = \
# 	testclient.c

# gconf_testclient_LDADD = $(GLIB_LIBS) $(OAF_LIBS) libgconf-client.la

gconftool_2_SOURCES = \
	gconftool.c

gconftool_2_LDADD = $(EFENCE) $(INTLLIBS) $(DEPENDENT_WITH_XML_LIBS) $(POPT_LIBS) libgconf-$(MAJOR_VERSION).la

$(CORBA_SOURCES): $(srcdir)/GConf.idl $(ORBIT_IDL)
	$(ORBIT_IDL) $(srcdir)/GConf.idl

libgconf_2_la_SOURCES = \
	gconf-internals.c	\
	gconf-internals.h	\
	gconf-backend.h		\
	gconf-backend.c		\
	gconf-changeset.c	\
	gconf-error.c		\
	gconf-listeners.c	\
	gconf-locale.h  	\
	gconf-locale.c  	\
	gconf-schema.c		\
	gconf-sources.c		\
	gconf-value.c		\
	gconf.c			\
	gconf-client.c		\
	gconf-enum-types.c	\
	$(CORBA_SOURCES)

libgconf_2_la_LDFLAGS = -version-info $(GCONF_CURRENT):$(GCONF_REVISION):$(GCONF_AGE)

libgconf_2_la_LIBADD = $(INTLLIBS) $(DEPENDENT_LIBS)

EXTRA_DIST=GConf.idl default.path.in gconfmarshal.list

install-exec-local:
	(cd $(DESTDIR)$(bindir) && $(RM) gconftool && $(LN_S) -f gconftool-$(MAJOR_VERSION) gconftool)

install-data-local:
	-mkdir -p $(DESTDIR)$(sysconfdir)/gconf/$(MAJOR_VERSION)
	$(INSTALL_DATA) default.path $(DESTDIR)$(sysconfdir)/gconf/$(MAJOR_VERSION)/path

gconfmarshal.h: @REBUILD@ stamp-gconfmarshal.h
	@true

gconfmarshal.c: @REBUILD@ stamp-gconfmarshal.c
	@true

stamp-gconfmarshal.h: @REBUILD@ gconfmarshal.list
	cd $(srcdir) \
	&& glib-genmarshal --prefix=gconf_marshal gconfmarshal.list --header >> xgen-gmh \
	&& (cmp -s xgen-gmh gconfmarshal.h || cp xgen-gmh gconfmarshal.h) \
	&& rm -f xgen-gmh xgen-gmh~

stamp-gconfmarshal.c: @REBUILD@ gconfmarshal.list
	cd $(srcdir) \
	&& glib-genmarshal --prefix=gconf_marshal gconfmarshal.list --body >> xgen-gmc \
	&& (cmp -s xgen-gmc gconfmarshal.c || cp xgen-gmc gconfmarshal.c) \
	&& rm -f xgen-gmc xgen-gmc~


gconf-enum-types.h: s-enum-types-h
	@true

FIX_ENUM_FILES=sed -e 's/g_conf/gconf/g' -e 's/TYPE_CONF/TYPE/g'

s-enum-types-h: @REBUILD@ $(gconf_headers) Makefile
	( cd $(srcdir) && glib-mkenums														\
			--fhead "#ifndef __GCONF_ENUM_TYPES_H__\n#define __GCONF_ENUM_TYPES_H__\n\n#include <glib-object.h>\n\nG_BEGIN_DECLS\n"	\
			--fprod "/* enumerations from \"@filename@\" */\n"									\
			--vhead "GType @enum_name@_get_type (void);\n#define GCONF_TYPE_@ENUMSHORT@ (@enum_name@_get_type())\n"			\
			--ftail "G_END_DECLS\n\n#endif /* __GCONF_ENUM_TYPES_H__ */"								\
		$(gconf_headers) | $(FIX_ENUM_FILES) ) > tmp-gconf-enum-types.h									\
	&& (cmp -s tmp-gconf-enum-types.h $(srcdir)/gconf-enum-types.h || cp tmp-gconf-enum-types.h $(srcdir)/gconf-enum-types.h )		\
	&& rm -f tmp-gconf-enum-types.h														\
	&& echo timestamp > $(@F)

gconf-enum-types.c: s-enum-types-c
	@true

s-enum-types-c: @REBUILD@ $(gconf_headers) Makefile
	( cd $(srcdir) && glib-mkenums																    \
			--fhead "#include \"gconf-client.h\""														    \
			--fprod "\n/* enumerations from \"@filename@\" */"											    \
			--vhead "GType\n@enum_name@_get_type (void)\n{\n  static GType etype = 0;\n  if (etype == 0) {\n    static const G@Type@Value values[] = {" \
			--vprod "      { @VALUENAME@, \"@VALUENAME@\", \"@valuenick@\" },"									    \
			--vtail "      { 0, NULL, NULL }\n    };\n    etype = g_@type@_register_static (\"@EnumName@\", values);\n  }\n  return etype;\n}\n"	    \
		$(gconf_headers) ) | $(FIX_ENUM_FILES) > tmp-gconf-enum-types.c											    \
	&& (cmp -s tmp-gconf-enum-types.c $(srcdir)/gconf-enum-types.c || cp tmp-gconf-enum-types.c $(srcdir)/gconf-enum-types.c )				    \
	&& rm -f tmp-gconf-enum-types.c																    \
	&& echo timestamp > $(@F)

