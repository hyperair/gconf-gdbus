
SGML_FILES =			\
	gconf.sgml

EXTRA_DIST=$(SGML_FILES)

all:

gconf/gconf.html: gconf.sgml
	-db2html $<

gconf.ps: gconf.sgml
	-db2ps $<

gconf.rtf: gconf.sgml	
	-db2rtf $<
