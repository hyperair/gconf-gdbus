#!/bin/sh

xgettext --default-domain=GConf --directory=.. \
  --add-comments --keyword=_ --keyword=N_ \
  --files-from=./POTFILES.in \
&& test ! -f GConf.po \
   || ( rm -f ./GConf.pot \
    && mv GConf.po ./GConf.pot )
