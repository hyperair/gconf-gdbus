
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

#include "scm-gconf.h"

#include "scm-gconf-private.h"

static long gconf_type_tag = 0;

#define SCM_TO_GCONF(obj) ((GConf*)(SCM_CDR(obj)))
#define GCONF_P(value)    (SCM_NIMP (value) && (SCM_CAR(value) == gconf_type_tag))

gboolean
scm_gconfp (SCM obj)
{
  return GCONF_P(obj);
}

GConf* 
scm2gconf (SCM obj)
{
  if (!GCONF_P(obj))
    return NULL;
  return SCM_TO_GCONF(obj);
}

SCM      
gconf2scm (GConf* conf)
{
  SCM smob;

  gh_defer_ints();

  SCM_NEWCELL (smob);
  SCM_SETCDR (smob, conf);
  SCM_SETCAR (smob, gconf_type_tag);

  gh_allow_ints();
  return smob;
}

static SCM
mark_gconf (SCM obj)
{
  return SCM_BOOL_F;
}

static scm_sizet
free_gconf (SCM obj)
{
  GConf* conf = SCM_TO_GCONF(obj);

  static const scm_sizet size = sizeof(GConf);

  gh_defer_ints();
  g_conf_unref(conf);
  gh_allow_ints();

  return size;
}

static int
print_gconf (SCM obj, SCM port, scm_print_state *pstate)
{
  GConf* conf = SCM_TO_GCONF(obj);

  scm_puts ("#<GConf configuration object>", port);

  /* non-zero means success */
  return 1;
}

static scm_smobfuns gconf_funcs = {
  mark_gconf, 
  free_gconf, 
  print_gconf, 
  0 // means we can never be equal? (FIXME gconfs should be able to be equal?) 
};

//// GConf routines

GCONF_PROC(gconfp,"gconf?",1,0,0,(SCM conf))
{
  return gh_bool2scm(GCONF_P(conf));
}

GCONF_PROC(make_gconf,"gconf-default",0,0,0,())
{
  GConf* conf;
  gh_defer_ints();
  conf = g_conf_new();
  scm_done_malloc(sizeof(GConf));
  gh_allow_ints();
  return gconf2scm(conf);
}

GCONF_PROC(get_value,"gconf-get",2,0,0,(SCM obj, SCM keyname))
{
  
  return SCM_BOOL_F;
}

void
g_conf_init_scm()
{
  gconf_type_tag = scm_newsmob(&gconf_funcs);

#include "scm-gconf.x"

}
