
## Install schema files

schemadir=$(sysconfdir)/gconf/schemas
schema_DATA=desktop.schemas


install-data-local:
	GCONF_CONFIG_SOURCE=$(GCONF_CONFIG_SOURCE) $(top_builddir)/gconf/gconftool --makefile-install-rule $(srcdir)/$(schema_DATA)
