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


static const gchar* err_msgs[11] = {
  N_("Success"),
  N_("Failed"),
  N_("Configuration server couldn't be contacted"),
  N_("Permission denied"),
  N_("Couldn't resolve address for configuration source"),
  N_("Bad key or directory name"),
  N_("Parse error"),
  N_("Type mismatch"),
  N_("Key operation on directory"),
  N_("Directory operation on key"),
  N_("Can't overwrite existing read-only value")
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

GConfError*
gconf_error_new(GConfErrNo en, const gchar* fmt, ...)
{
  GConfErrorPrivate* priv;
  va_list args;
  
  priv = g_new(GConfErrorPrivate, 1);
  
  va_start (args, fmt);
  priv->str = g_strdup_vprintf(fmt, args);
  va_end (args);

  priv->str = g_strconcat(gconf_strerror(en), ":\n ", priv->str, NULL);

  priv->num = en;

  return (GConfError*)priv;
}

void
gconf_error_destroy(GConfError* err)
{
  GConfErrorPrivate* priv = (GConfErrorPrivate*)err;

  g_return_if_fail(err != NULL);
  
  g_free(priv->str);
  g_free(priv);
}




