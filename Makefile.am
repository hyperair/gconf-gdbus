
SUBDIRS = gconf backends po doc examples

DIST_SUBDIRS = tests gconf backends po doc examples defaults

if ENABLE_DEFAULTS_SERVICE
SUBDIRS += defaults
endif

EXTRA_DIST =                \
	TODO                \
	gtk-doc.make        \
	intltool-update.in  \
	intltool-extract.in \
	intltool-merge.in

DISTCLEANFILES =          \
	intltool-update   \
	intltool-merge    \
	intltool-extract

DISTCHECK_CONFIGURE_FLAGS = --enable-gtk-doc --enable-defaults-service

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
