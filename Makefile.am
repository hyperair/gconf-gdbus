
SUBDIRS = gconf backends wrappers po intl doc examples standard-schemas
DIST_SUBDIRS=tests $(SUBDIRS)

EXTRA_DIST=gconf-config.in gconf.m4.in gconf.spec.in gconf.spec

WARNING_MESSAGE="If this is your first install of GConf, you may need to move $(prefix)/etc/gconf/$(MAJOR_VERSION)/path.example to $(prefix)/etc/gconf/$(MAJOR_VERSION)/path, so that the default configuration is sane. If you are already using GConf, you may need to move your old configuration into place if $(MAJOR_VERSION) is not the same as your old version."

all-local:
	echo "**** NOTE ****" && \
	echo $(WARNING_MESSAGE) && \
	echo "**************"

install-exec-local:
	$(INSTALL_SCRIPT) gconf-config $(bindir)/gconf-config-$(MAJOR_VERSION)
	$(LN_S) -f $(bindir)/gconf-config-$(MAJOR_VERSION) $(bindir)/gconf-config
	echo "**** NOTE ****" && \
	echo $(WARNING_MESSAGE) && \
	echo "**************"

install-data-local:
	-mkdir -p $(datadir)/aclocal
	$(INSTALL_DATA) gconf.m4 $(datadir)/aclocal/gconf-$(MAJOR_VERSION).m4
	echo "**** NOTE ****" && \
	echo $(WARNING_MESSAGE) && \
	echo "**************"

