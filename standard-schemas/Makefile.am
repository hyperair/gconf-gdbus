
## Install schema files

## NOTE this file will be in multiple stable releases of GConf, 
## which means it should probably go in a separate RPM

schemadir=$(sysconfdir)/gconf/schemas
schema_DATA=desktop.schemas

EXTRA_DIST=$(schema_DATA)

install-data-local:
	GCONF_CONFIG_SOURCE=$(GCONF_CONFIG_SOURCE) $(top_builddir)/gconf/gconftool-$(MAJOR_VERSION) --makefile-install-rule $(srcdir)/$(schema_DATA)
