#!/bin/sh

xgettext --default-domain=gconf --directory=.. \
  --add-comments --keyword=_ --keyword=N_ \
  --files-from=./POTFILES.in \
&& test ! -f gconf.po \
   || ( rm -f ./gconf.pot \
    && mv gconf.po ./gconf.pot )
