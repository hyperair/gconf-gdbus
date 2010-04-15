module_flags = -export_dynamic -avoid-version -module -no-undefined -export-symbols-regex '^g_io_module_(load|unload|query)'

giomodule_LTLIBRARIES = libgsettingsgconfbackend.la
giomoduledir = $(GIO_MODULE_DIR)

libgsettingsgconfbackend_la_SOURCES = \
	gconfsettingsbackend-module.c \
	gconfsettingsbackend.c        \
	gconfsettingsbackend.h

libgsettingsgconfbackend_la_CFLAGS = \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	$(GSETTINGS_CFLAGS)

libgsettingsgconfbackend_la_LDFLAGS = \
	$(module_flags)               \
	$(NULL)

libgsettingsgconfbackend_la_LIBADD =        \
	$(top_builddir)/gconf/libgconf-2.la \
	$(GSETTINGS_LIBS)                   \
	$(NULL)

install-data-hook:
	if test -z "$(DESTDIR)" -a "$(GIO_QUERYMODULES)" != "no" ; then                         \
		$(GIO_QUERYMODULES) $(GIO_MODULE_DIR) ;                                         \
	fi
