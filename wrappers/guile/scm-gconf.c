
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

#define SCM_TO_GCONF(obj) ((GConfEngine*)(SCM_CDR(obj)))
#define GCONF_P(value)    (SCM_NIMP (value) && (SCM_CAR(value) == gconf_type_tag))

gboolean
scm_gconfp (SCM obj)
{
  return GCONF_P(obj);
}

GConfEngine* 
scm2gconf (SCM obj)
{
  if (!GCONF_P(obj))
    return NULL;
  return SCM_TO_GCONF(obj);
}

SCM      
gconf2scm (GConfEngine* conf)
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
  GConfEngine* conf = SCM_TO_GCONF(obj);

  static const scm_sizet size = sizeof(GConfEngine);

  gh_defer_ints();
  g_conf_engine_unref(conf);
  gh_allow_ints();

  return size;
}

static int
print_gconf (SCM obj, SCM port, scm_print_state *pstate)
{
  GConfEngine* conf = SCM_TO_GCONF(obj);

  scm_puts ("#<GConf configuration object>", port);

  /* non-zero means success */
  return 1;
}

static scm_smobfuns gconf_funcs = {
  mark_gconf, 
  free_gconf, 
  print_gconf, 
  0 /* means we can never be equal? */
};

/*
 * Assorted utility stuff
 */

SCM
g_conf_value_to_scm(GConfValue* val)
{
  SCM retval = SCM_EOL;

  if (val == NULL)
    return SCM_EOL;
  
  switch (val->type)
    {
    case G_CONF_VALUE_INVALID:
      /* EOL */
      break;
    case G_CONF_VALUE_STRING:
      retval = gh_str02scm(g_conf_value_string(val));
      break;
    case G_CONF_VALUE_INT:
      retval = gh_int2scm(g_conf_value_int(val));
      break;
    case G_CONF_VALUE_FLOAT:
      retval = gh_double2scm(g_conf_value_float(val));
      break;
    case G_CONF_VALUE_BOOL:
      retval = gh_bool2scm(g_conf_value_bool(val));
      break;
    case G_CONF_VALUE_SCHEMA:
      /* FIXME this is more complicated, we need a smob or something */
      break;
    case G_CONF_VALUE_LIST:
      /* FIXME This is complicated too... */
      break;
    case G_CONF_VALUE_PAIR:
      retval = gh_cons(g_conf_value_to_scm(g_conf_value_car(val)),
                       g_conf_value_to_scm(g_conf_value_cdr(val)));
      break;
    default:
      g_warning("Unhandled type in %s", __FUNCTION__);
      break;
    }

  return retval;
}

GConfValue*
g_conf_value_new_from_scm(SCM obj)
{


  return NULL;
}

/*
 * GConf routines
 */

GCONF_PROC(gconfp,"gconf?",1,0,0,(SCM conf))
{
  return gh_bool2scm(GCONF_P(conf));
}

GCONF_PROC(make_gconf,"gconf-default",0,0,0,())
{
  GConfEngine* conf;
  gh_defer_ints();
  conf = g_conf_engine_new();
  scm_done_malloc(sizeof(GConfEngine));
  gh_allow_ints();
  return gconf2scm(conf);
}

GCONF_PROC(get_value,"gconf-get",2,0,0,(SCM obj, SCM keyname))
{
  char* str;
  GConfValue* val;
  SCM retval;
  GConfError* err = NULL;
  
  SCM_ASSERT(GCONF_P(obj), obj, SCM_ARG1, "gconf-get");
  SCM_ASSERT(gh_string_p(keyname), keyname, SCM_ARG2, "gconf-get");

  gh_defer_ints();
  
  str = gh_scm2newstr(keyname, NULL);
  
  val = g_conf_get(SCM_TO_GCONF(obj), str, &err);

  free(str);

  if (val == NULL &&
      err != NULL)
    {
      printf("Failed: %s\n", err->str);
      g_conf_error_destroy(err);
      err = NULL;
      retval = SCM_EOL;
    }
  else
    {
      /* NULL val is OK */
      retval = g_conf_value_to_scm(val);
      
      if (val)
        g_conf_value_destroy(val);
    }
      
  gh_allow_ints();

  return retval;
}

void
g_conf_init_scm()
{
  gconf_type_tag = scm_newsmob(&gconf_funcs);

#include "scm-gconf.x"

}
