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

typedef struct _GConfErrorPrivate GConfErrorPrivate;

struct _GConfErrorPrivate {
  /* keep these members in sync with GConfError */
  gchar* str; /* convert to non-const */
  GConfErrNo num;
};

GConfError*
g_conf_error_new(GConfErrNo en, const gchar* fmt, ...)
{
  GConfErrorPrivate* priv;
  
  priv = g_new(GConfErrorPrivate, 1);

  va_list args;
  
  va_start (args, fmt);
  priv->str = details = g_strdup_vprintf(fmt, args);
  va_end (args);

  priv->str = g_strconcat(g_conf_strerror(en), ":\n ", priv->str, NULL);

  priv->num = en;
}

void
g_conf_error_destroy(GConfError* err)
{
  GConfErrorPrivate* priv = (GConfErrorPrivate*)err;
  
  g_free(priv->str);
  g_free(priv);
}




