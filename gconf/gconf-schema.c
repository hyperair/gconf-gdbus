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

#include "gconf-schema.h"


GConfSchema*  
gconf_schema_new(void)
{
  GConfSchema* sc;

  sc = g_new0(GConfSchema, 1);

  sc->type = GCONF_VALUE_INVALID;

  return sc;
}

void          
gconf_schema_destroy(GConfSchema* sc)
{
  if (sc->short_desc)
    g_free(sc->short_desc);

  if (sc->long_desc)
    g_free(sc->long_desc);

  if (sc->owner)
    g_free(sc->owner);

  g_free(sc);
}

GConfSchema*  
gconf_schema_copy(GConfSchema* sc)
{
  GConfSchema* dest;

  dest = gconf_schema_new();

  dest->type = sc->type;

  dest->short_desc = g_strdup(sc->short_desc);

  dest->long_desc = g_strdup(sc->long_desc);

  dest->owner = g_strdup(sc->owner);  

  return dest;
}

void          
gconf_schema_set_type(GConfSchema* sc, GConfValueType type)
{
  sc->type = type;
}

void          
gconf_schema_set_short_desc(GConfSchema* sc, const gchar* desc)
{
  if (sc->short_desc)
    g_free(sc->short_desc);

  if (desc)
    sc->short_desc = g_strdup(desc);
  else 
    sc->short_desc = NULL;
}

void          
gconf_schema_set_long_desc(GConfSchema* sc, const gchar* desc)
{
  if (sc->long_desc)
    g_free(sc->long_desc);

  if (desc)
    sc->long_desc = g_strdup(desc);
  else 
    sc->long_desc = NULL;
}

void          
gconf_schema_set_owner(GConfSchema* sc, const gchar* owner)
{
  if (sc->owner)
    g_free(sc->owner);

  if (owner)
    sc->owner = g_strdup(owner);
  else
    sc->owner = NULL;
}

void
gconf_schema_set_default_value(GConfSchema* sc, GConfValue* val)
{
  if (sc->default_value != NULL)
    gconf_value_destroy(sc->default_value);

  sc->default_value = gconf_value_copy(val);
}

void
gconf_schema_set_default_value_nocopy(GConfSchema* sc, GConfValue* val)
{
  if (sc->default_value != NULL)
    gconf_value_destroy(sc->default_value);

  sc->default_value = val;
}
