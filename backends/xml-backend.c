
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


#include <gconf/gconf-backend.h>

static void          shutdown        (GConfBackend* backend);

static GConfSource*  resolve_address (GConfBackend* backend, const gchar* address);

static GConfValue*   query_value     (GConfSource* source, const gchar* key);

static void          destroy_source  (GConfSource* source);

static GConfBackendVTable xml_vtable = {
  shutdown,
  resolve_address,
  query_value,
  destroy_source
};

static void          
shutdown (GConfBackend* backend)
{

}

static GConfSource*  
resolve_address (GConfBackend* backend, const gchar* address)
{

}

static GConfValue* 
query_value (GConfSource* source, const gchar* key)
{
  

}

static void          
destroy_source  (GConfSource* source)
{


}

/* Initializer */

G_MODULE_EXPORT const gchar*
g_module_check_init (GModule *module)
{
  g_print("Init XML module\n");

  return NULL;
}

G_MODULE_EXPORT GConfBackendVTable* 
g_conf_backend_get_vtable(void)
{
  return &xml_vtable;
}



