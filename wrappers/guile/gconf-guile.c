
/* GConf
 * Copyright (C) 1999 Red Hat Inc.
 * Developed by Havoc Pennington
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

#include <config.h>

#include "scm-gconf.h"


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


static void
real_main(void* closure, int argc, char* argv[])
{
  GConfError* err = NULL;
  
  if (g_conf_init_orb(&argc, argv, &err) == CORBA_OBJECT_NIL)
    {
      fprintf(stderr, _("Failed to init orb: %s\n"), err->str);
      g_conf_error_destroy(err);
      exit(1);
    }
  
  if (!g_conf_init(&err))
    {
      fprintf(stderr, _("Failed to init gconf: %s\n"), err->str);
      g_conf_error_destroy(err);
      exit(1);
    }
  
  g_conf_init_scm();

  gh_eval_str("(set-repl-prompt! \"gconf> \")");

  scm_shell(argc, argv);
}

int
main(int argc, char* argv[])
{
  scm_boot_guile(argc, argv, real_main, 0);
  return 0; // never reached
}

