/* GConf
 * Copyright (C) 1999 Red Hat Inc.
 * This file is basically a libgnorba cut-and-paste,
 *  but it relies on a cookie in the user's home dir instead
 *  of the X property (one gconfd per user and home dir)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <ctype.h>
#include <errno.h>
#include <pwd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <time.h>
#include "gconf-internals.h"
#include <liboaf/liboaf.h>

/* Quick hack so I can mark strings */

#ifdef _ 
#warning "_ already defined"
#else
#define _(x) x
#endif

#ifdef N_ 
#warning "N_ already defined"
#else
#define N_(x) x
#endif

CORBA_ORB
gconf_init_orb(int* argc, char** argv, GConfError** err)
{
  CORBA_ORB retval;

  retval = oaf_init(*argc, argv);
	
  if (!retval)
    {
      if (err)
        *err = gconf_error_new(GCONF_FAILED, _("Failure initializing ORB"));
      return CORBA_OBJECT_NIL;
    }

  return retval;
}
