
HTML_DIR=$(datadir)

# The name of the module.
DOC_MODULE=gconf

# The top-level SGML file.
DOC_MAIN_SGML_FILE=gconf.sgml

TARGET_DIR=$(HTML_DIR)/$(DOC_MODULE)/$(MAJOR_VERSION)

# not sure this will work...
GTKDOC_LD=$(LIBTOOL) --mode=link $(CC)
GTKDOC_CC=$(LIBTOOL) --mode=compile $(CC)
LDFLAGS=$(DEPENDENT_LIBS) $(top_builddir)/gconf/libgconf-$(MAJOR_VERSION).la

CFLAGS= -I$(top_srcdir) $(DEPENDENT_CFLAGS)

scan:
	(cd $(srcdir) && env LD="$(GTKDOC_LD)" CC="$(GTKDOC_CC)" CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)" gtkdoc-scangobj --module=$(DOC_MODULE) --nogtkinit)
	gtkdoc-scan --module=$(DOC_MODULE) --source-dir=$(top_srcdir)

templates: scan
	gtkdoc-mktmpl --module=$(DOC_MODULE)

sgml:
	gtkdoc-mkdb --module=$(DOC_MODULE)

html:
	if ! test -d html ; then mkdir html ; fi
	-cd html && gtkdoc-mkhtml $(DOC_MODULE) ../$(DOC_MAIN_SGML_FILE)

clean-local:
	rm -f *~ *.bak *.hierarchy *.signals *.args *-unused.txt

maintainer-clean-local: clean
	rm -rf sgml html $(DOC_MODULE)-decl-list.txt $(DOC_MODULE)-decl.txt 

install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(TARGET_DIR) ## seems ok
	-$(INSTALL_DATA) $(srcdir)/html/*.html $(DESTDIR)$(TARGET_DIR)
	-$(INSTALL_DATA) $(srcdir)/html/index.sgml $(DESTDIR)$(TARGET_DIR)
	-gtkdoc-fixxref --module=$(DOC_MODULE) --html-dir=$(DESTDIR)$(HTML_DIR)

EXTRA_DIST = \
	$(DOC_MAIN_SGML_FILE) \
	$(DOC_MODULE)-sections.txt \
	gconf-decl.txt

dist-hook:
	$(mkinstalldirs) $(distdir)/tmpl
	cp -p $(srcdir)/tmpl/*.sgml $(distdir)/tmpl

.PHONY : html sgml templates scan

