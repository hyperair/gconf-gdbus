
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
#include <gconf/gconf-internals.h>

/*
 * XML storage implementation
 */

typedef struct _XMLSource XMLSource;

struct _XMLSource {
  GConfSource source;
  
};

static XMLSource* 
xconf_source_new(const gchar* root_dir)
{
  
  return g_new0(XMLSource, 1);
}

static void
xconf_source_destroy(XMLSource* source)
{
  g_free(source);

}

/*
 * Dyna-load implementation
 */

static void          shutdown        (void);

static GConfSource*  resolve_address (const gchar* address);

static GConfValue*   query_value     (GConfSource* source, const gchar* key);

static void          destroy_source  (GConfSource* source);

static GConfBackendVTable xml_vtable = {
  shutdown,
  resolve_address,
  query_value,
  destroy_source
};

static void          
shutdown (void)
{
  printf("Shutting down XML module\n");
}

static GConfSource*  
resolve_address (const gchar* address)
{
  gchar* root_dir;
  XMLSource* xsource;
  
  root_dir = g_conf_address_resource(address);

  if (root_dir == NULL)
    {
      g_warning("Bad address");
      return NULL;
    }

  xsource = xconf_source_new(root_dir);

  g_free(root_dir);

  return (GConfSource*)xsource;
}

static GConfValue* 
query_value (GConfSource* source, const gchar* key)
{
  
  return NULL;
}

static void          
destroy_source  (GConfSource* source)
{
  xconf_source_destroy((XMLSource*)source);
}

/* Initializer */

G_MODULE_EXPORT const gchar*
g_module_check_init (GModule *module)
{
  printf("Initializing XML module\n");

  return NULL;
}

G_MODULE_EXPORT GConfBackendVTable* 
g_conf_backend_get_vtable(void)
{
  return &xml_vtable;
}



