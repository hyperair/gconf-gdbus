
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

#include "gconf-internals.h"
#include "gconf-backend.h"

GConfValue* 
g_conf_value_new(GConfValueType type)
{
  GConfValue* value;

  value = g_new0(GConfValue, 1);

  value->type = type;

  return value;
}

void 
g_conf_value_destroy(GConfValue* value)
{
  g_return_if_fail(value != NULL);

  if (value->type == G_CONF_VALUE_STRING && 
      value->d.string_data != NULL)
    g_free(value->d.string_data);

  g_free(value);
}

void        
g_conf_value_set_int(GConfValue* value, gint the_int)
{
  g_return_if_fail(value != NULL);
  g_return_if_fail(value->type == G_CONF_VALUE_INT);

  value->d.int_data = the_int;
}

void        
g_conf_value_set_string(GConfValue* value, const gchar* the_str)
{
  g_return_if_fail(value != NULL);
  g_return_if_fail(value->type == G_CONF_VALUE_STRING);

  value->d.string_data = g_strdup(the_str);
}

void        
g_conf_value_set_float(GConfValue* value, gdouble the_float)
{
  g_return_if_fail(value != NULL);
  g_return_if_fail(value->type == G_CONF_VALUE_FLOAT);

  value->d.float_data = the_float;
}


/* 
 *  Sources
 */

GConfSource* 
g_conf_resolve_address(const gchar* address)
{
  GConfBackend* backend;

  backend = g_conf_get_backend(address);

  if (backend == NULL)
    return NULL;
  else
    {
      GConfSource* retval;

      retval = g_conf_backend_resolve_address(backend, address);

      if (retval == NULL)
        {
          g_conf_backend_unref(backend);
          return NULL;
        }
      else
        {
          retval->backend = backend;
          
          /* Leave a ref on the backend, now held by the GConfSource */
          
          return retval;
        }
    }
}

GConfValue*   
g_conf_source_query_value      (GConfSource* source,
                                const gchar* key)
{
  return (*source->backend->vtable->query_value)(source, key);
}

void         
g_conf_source_destroy (GConfSource* source)
{
  GConfBackend* backend = source->backend;

  (*source->backend->vtable->destroy_source)(source);

  /* Remove ref held by the source. */
  g_conf_backend_unref(backend);
}
