/* GConf
 * Copyright (C) 1999 Red Hat Inc.
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

#include "gconf-error.h"
#include <stdarg.h>

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


static const gchar* err_msgs[] = {
  N_("Success"),
  N_("Failed"),
  N_("Configuration server couldn't be contacted"),
  N_("Permission denied"),
  N_("Couldn't resolve address for configuration source"),
  N_("Bad key or directory name"),
  N_("Parse error"),
  N_("Corrupt data in configuration source database"),
  N_("Type mismatch"),
  N_("Key operation on directory"),
  N_("Directory operation on key"),
  N_("Can't overwrite existing read-only value"),
  N_("Object Activation Framework error"),
  N_("Operation not allowed without configuration server"),
  N_("Failed to get a lock")
};

static const int n_err_msgs = sizeof(err_msgs)/sizeof(err_msgs[0]);

const gchar* 
gconf_strerror       (GConfErrNo en)
{
  g_return_val_if_fail (en < n_err_msgs, NULL);

  return _(err_msgs[en]);    
}

typedef struct _GConfErrorPrivate GConfErrorPrivate;

struct _GConfErrorPrivate {
  /* keep these members in sync with GConfError */
  gchar* str; /* convert to non-const */
  GConfErrNo num;
};

static GConfError* 
gconf_error_new_valist(GConfErrNo en, const gchar* fmt, va_list args)
{
  GConfErrorPrivate* priv;
  gchar* orig;
  
  priv = g_new(GConfErrorPrivate, 1);

  priv->str = g_strdup_vprintf(fmt, args);

  orig = priv->str;
  
  priv->str = g_strconcat(gconf_strerror(en), ":\n ", orig, NULL);

  g_free(orig);
  
  priv->num = en;

  return (GConfError*)priv;
}

GConfError*
gconf_error_new(GConfErrNo en, const gchar* fmt, ...)
{
  GConfError* err;
  va_list args;
  
  va_start (args, fmt);
  err = gconf_error_new_valist(en, fmt, args);
  va_end (args);

  return err;
}

void
gconf_error_destroy(GConfError* err)
{
  GConfErrorPrivate* priv = (GConfErrorPrivate*)err;

  g_return_if_fail(err != NULL);
  
  g_free(priv->str);
  g_free(priv);
}

GConfError*
gconf_error_copy     (GConfError* err)
{
  GConfErrorPrivate* priv = (GConfErrorPrivate*)err;
  GConfErrorPrivate* new;

  g_return_val_if_fail(err != NULL, NULL);
  
  new = g_new(GConfErrorPrivate, 1);

  new->str = g_strdup(priv->str);
  new->num  = priv->num;

  return (GConfError*)new;
}

void
gconf_set_error      (GConfError** err, GConfErrNo en, const gchar* fmt, ...)
{
  GConfError* obj;
  va_list args;

  if (err == NULL)
    return;

  /* Warn if we stack up errors on top
   * of each other. Keep the "deepest"
   * error
   */
  g_return_if_fail(*err == NULL);
  
  va_start (args, fmt);
  obj = gconf_error_new_valist(en, fmt, args);
  va_end (args);

  *err = obj;
}

void
gconf_clear_error    (GConfError** err)
{
  if (err && *err)
    {
      gconf_error_destroy(*err);
      *err = NULL;
    }
}

GConfError*
gconf_compose_errors (GConfError* err1, GConfError* err2)
{
  if (err1 == NULL && err2 == NULL)
    return NULL;
  else if (err1 == NULL)
    return gconf_error_copy(err2);
  else if (err2 == NULL)
    return gconf_error_copy(err1);
  else
    {
      GConfErrorPrivate* priv;

      priv = g_new(GConfErrorPrivate, 1);

      if (err1->num == err2->num)
        priv->num = err1->num;
      else
        priv->num = GCONF_FAILED;

      priv->str = g_strconcat(err1->str, "\n", err2->str, NULL);

      return (GConfError*)priv;
    }
}
