
SUBDIRS = gconf backends po doc examples
DIST_SUBDIRS=tests $(SUBDIRS)

EXTRA_DIST =            \
	gconf-2.m4.in   \
	TODO            \
	gconf-2.0.pc.in \
	gtk-doc.make

DISTCHECK_CONFIGURE_FLAGS = --enable-gtk-doc
DISTCLEANFILES =                \
	intltool-extract        \
	intltool-merge          \
	intltool-update

install-data-local:
	-mkdir -p $(DESTDIR)$(sysgconfdir)/gconf.xml.defaults
	-chmod 755 $(DESTDIR)$(sysgconfdir)/gconf.xml.defaults
	-mkdir -p $(DESTDIR)$(sysgconfdir)/gconf.xml.mandatory
	-chmod 755 $(DESTDIR)$(sysgconfdir)/gconf.xml.mandatory

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = gconf-2.0.pc

aclocaldir = $(datadir)/aclocal
aclocal_DATA = gconf-2.m4

install-schemas:
	(cd standard-schemas && $(MAKE) $(AM_MAKEFLAGS) install-schemas)
