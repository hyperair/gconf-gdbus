
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


#include "gconf-backend.h"
#include "gconf-internals.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Quick hack so I can mark strings */

#ifdef _ 
#warning "_ already defined in util.h"
#else
#define _(x) x
#endif

#ifdef N_ 
#warning "N_ already defined in util.h"
#else
#define N_(x) x
#endif


gchar* 
g_conf_address_backend(const gchar* address)
{
  const gchar* end;

  g_return_val_if_fail(address != NULL, NULL);

  end = strchr(address, ':');

  if (end == NULL)
    {
      return NULL;
    }
  else
    {
      int len = end-address+1;
      gchar* retval = g_malloc(len);
      strncpy(retval, address, len-1);
      retval[len-1] = '\0';
      return retval;
    }
}

gchar* 
g_conf_address_resource(const gchar* address)
{
  const gchar* start;

  g_return_val_if_fail(address != NULL, NULL);

  start = strchr(address, ':');

  if (start == NULL)
    {
      return NULL;
    }
  else
    {
      ++start;
      return g_strdup(start);
    }
}

const gchar* 
g_conf_backend_dir(void)
{
  /* Obviously not permanent... :-) */
  return "../backends/.libs/";
}

static gboolean
g_file_exists (const char *filename)
{
	struct stat s;

	g_return_val_if_fail (filename != NULL,FALSE);
	
	return stat (filename, &s) == 0;
}

gchar*       
g_conf_backend_file(const gchar* address)
{
  gchar* back;
  gchar* file;
  gchar* retval;

  g_return_val_if_fail(address != NULL, NULL);

  back = g_conf_address_backend(address);

  if (back == NULL)
    return NULL;

  file = g_strconcat("libgconfbackend-", back, ".so");
  
  retval = g_strconcat(g_conf_backend_dir(), file, NULL);

  g_free(back);
  g_free(file);

  if (retval != NULL && 
      g_file_exists(retval))
    {
      return retval;
    }
  else
    {
      if (retval)
        g_warning("No such file `%s'\n", retval);
      g_free(retval);
      return NULL;
    }
}

/*
 * Backend Cache 
 */

static GHashTable* loaded_backends = NULL;

GConfBackend* 
g_conf_get_backend(const gchar* address)
{
  GConfBackend* backend;
  gchar* name;

  if (loaded_backends == NULL)
    {
      loaded_backends = g_hash_table_new(g_str_hash, g_str_equal);
    }
  name = g_conf_address_backend(address);
      
  if (name == NULL)
    {
      g_warning("Bad address `%s'", address);
      return NULL;
    }

  backend = g_hash_table_lookup(loaded_backends, name);

  if (backend != NULL)
    return backend;
  else
    {
      gchar* file;
          
      file = g_conf_backend_file(address);
          
      if (file != NULL)
        {
          GModule* module;
          GConfBackendVTable* (*get_vtable)(void);

          if (!g_module_supported())
            g_error(_("GConf won't work without dynamic module support (gmodule)"));
              
          module = g_module_open(file, 0);
              
          g_free(file);
          
          if (module == NULL)
            {
              gchar* error = g_module_error();
              fprintf(stderr, _("Error opening module `%s': %s\n"),
                      name, error);
              g_free(name);
              return NULL;
            }

          if (!g_module_symbol(module, 
                               "g_conf_backend_get_vtable", 
                               (gpointer*)&get_vtable))
            {
              g_free(name);
              return NULL;
            }
          
          backend = g_new0(GConfBackend, 1);

          backend->module = module;

          backend->vtable = (*get_vtable)();
              
          backend->name = name;

          g_hash_table_insert(loaded_backends, (gchar*)backend->name, backend);
          
          /* Returning a "copy" */
          g_conf_backend_ref(backend);

          return backend;
        }
      else
        {
          g_warning("Couldn't locate file for `%s'", address);
          return NULL;
        }
    }
}

void          
g_conf_backend_ref(GConfBackend* backend)
{
  g_return_if_fail(backend != NULL);

  backend->refcount += 1;
}

void          
g_conf_backend_unref(GConfBackend* backend)
{
  g_return_if_fail(backend != NULL);
  g_return_if_fail(backend->refcount > 0);

  if (backend->refcount > 1)
    {
      backend->refcount -= 1;
    }
  else
    {
      (*backend->vtable->shutdown)();

      if (!g_module_close(backend->module))
        g_warning(_("Failed to shut down backend"));

      g_hash_table_remove(loaded_backends, backend->name);
      
      g_free((gchar*)backend->name); /* cast off const */

      g_free(backend);
    }  
}

/*
 * Backend vtable wrappers
 */

GConfSource*  
g_conf_backend_resolve_address (GConfBackend* backend, 
                                const gchar* address)
{
  return (*backend->vtable->resolve_address)(address);

}



