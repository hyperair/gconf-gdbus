
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
#include "gconf-sources.h"
#include "gconf-internals.h"
#include "gconfd-error.h"
#include "gconf-schema.h"
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>


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

/* hack until we move server to new error model */
static gboolean
g_conf_key_check_hack(const gchar* key)
{
  GConfError* err;
  gboolean retval;

  retval = g_conf_key_check(key, &err);

  if (retval)
    return TRUE;
  else
    {
      g_conf_set_error(err->num, err->str);
      g_conf_error_destroy(err);
      return FALSE;
    }
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
          retval->address = g_strdup(address);
          
          /* Leave a ref on the backend, now held by the GConfSource */
          
          return retval;
        }
    }
}

GConfValue*   
g_conf_source_query_value      (GConfSource* source,
                                const gchar* key,
                                gchar** schema_name)
{
  if (!g_conf_key_check_hack(key))
    return NULL;
  
  return (*source->backend->vtable->query_value)(source, key, schema_name);
}

void          
g_conf_source_set_value        (GConfSource* source,
                                const gchar* key,
                                GConfValue* value)
{
  g_return_if_fail(value != NULL);
  g_return_if_fail(key != NULL);

  if (!g_conf_key_check_hack(key))
    return;

  g_assert(*key != '\0');

  if (key[1] == '\0')
    {
      g_conf_set_error(G_CONF_IS_DIR, _("The '/' name can only be a directory, not a key"));
      return;
    }

  (*source->backend->vtable->set_value)(source, key, value);
}

void          
g_conf_source_unset_value      (GConfSource* source,
                                const gchar* key)
{
  if (!g_conf_key_check_hack(key))
    return;

  (*source->backend->vtable->unset_value)(source, key);
}

GSList*      
g_conf_source_all_entries         (GConfSource* source,
                                 const gchar* dir)
{
  if (!g_conf_key_check_hack(dir))
    return NULL;

  return (*source->backend->vtable->all_entries)(source, dir);
}

GSList*      
g_conf_source_all_dirs          (GConfSource* source,
                                 const gchar* dir)
{
  if (!g_conf_key_check_hack(dir))
    return NULL;

  return (*source->backend->vtable->all_subdirs)(source, dir);
}

gboolean
g_conf_source_dir_exists        (GConfSource* source,
                                 const gchar* dir)
{
  if (!g_conf_key_check_hack(dir))
    return FALSE;
  
  return (*source->backend->vtable->dir_exists)(source, dir);
}

void         
g_conf_source_remove_dir        (GConfSource* source,
                                 const gchar* dir)
{
  if (!g_conf_key_check_hack(dir))
    return;

  return (*source->backend->vtable->remove_dir)(source, dir);
}

void         
g_conf_source_set_schema        (GConfSource* source,
                                 const gchar* key,
                                 const gchar* schema_key)
{
  if (!g_conf_key_check_hack(key))
    return;

  if (!g_conf_key_check_hack(schema_key))
    return;
  
  return (*source->backend->vtable->set_schema)(source, key, schema_key);
}

gboolean
g_conf_source_sync_all         (GConfSource* source)
{
  return (*source->backend->vtable->sync_all)(source);
}

void         
g_conf_source_destroy (GConfSource* source)
{
  GConfBackend* backend = source->backend;

  (*source->backend->vtable->destroy_source)(source);

  /* Remove ref held by the source. */
  g_conf_backend_unref(backend);
}


/*
 *   Source stacks
 */

GConfSources* 
g_conf_sources_new(gchar** addresses)
{
  GConfSources* sources;
  GSList* failed = NULL;

  sources = g_new0(GConfSources, 1);

  while (*addresses != NULL)
    {
      GConfSource* source;

      source = g_conf_resolve_address(*addresses);

      if (source != NULL)
        sources->sources = g_list_prepend(sources->sources, source);
      else
        failed = g_slist_prepend(failed, *addresses);

      ++addresses;
    }

  sources->sources = g_list_reverse(sources->sources);

  if (failed != NULL)
    {
      GSList* tmp;
      gchar* all = g_strdup("");

      tmp = failed;

      while (tmp != NULL)
        {
          gchar* old = all;

          all = g_strconcat(old, ", ", tmp->data, NULL);

          g_free(old);

          tmp = g_slist_next(tmp);
        }
      
      g_conf_set_error(G_CONF_BAD_ADDRESS, 
                       _("The following config source addresses were not resolved:\n%s"),
                       all);
      g_free(all);
    }

  return sources;
}

void
g_conf_sources_destroy(GConfSources* sources)
{
  GList* tmp;

  tmp = sources->sources;

  while (tmp != NULL)
    {
      g_conf_source_destroy(tmp->data);
      
      tmp = g_list_next(tmp);
    }

  g_list_free(sources->sources);

  g_free(sources);
}

GConfValue*   
g_conf_sources_query_value (GConfSources* sources, 
                            const gchar* key)
{
  GList* tmp;
  gchar* schema_name = NULL;

  tmp = sources->sources;

  while (tmp != NULL)
    {
      GConfValue* val;

      g_conf_clear_error();

      val = g_conf_source_query_value(tmp->data, key,
                                      schema_name ? NULL : &schema_name); /* once we have one, no more. */

      if (val == NULL)
        {
          switch (g_conf_errno())
            {
            case G_CONF_BAD_KEY:
              /* this isn't getting any better, so bail */
              return NULL;
              break;
            case G_CONF_SUCCESS:
              break;
            default:
              /* weird error, try some other sources */
              break;
            }
        }
      else if (val->type == G_CONF_VALUE_IGNORE_SUBSEQUENT)
        {
          /* Bail now, instead of looking for the standard values */
          g_conf_value_destroy(val);
          break;
        }
      else
        return val;

      tmp = g_list_next(tmp);
    }

  /* If we got here, there was no value; we try to look up the
     schema for this key if we have one, and use the default
     value.
  */

  if (schema_name != NULL)
    {
      GConfValue* val =
        g_conf_sources_query_value(sources, schema_name);
      
      if (val != NULL &&
          val->type != G_CONF_VALUE_SCHEMA)
        {
          g_conf_set_error(G_CONF_FAILED, _("Schema `%s' specified for `%s' stores a non-schema value"), schema_name, key);
                
          g_free(schema_name);

          return NULL;
        }
      else if (val != NULL)
        {
          GConfValue* retval = g_conf_value_schema(val)->default_value;
          /* cheat, "unparent" the value to avoid a copy */
          g_conf_value_schema(val)->default_value = NULL;
          g_conf_value_destroy(val);

          g_free(schema_name);      
          
          return retval;
        }
      else
        {
          g_free(schema_name);
          
          return NULL;
        }
    }
  
  return NULL;
}


void
g_conf_sources_set_value   (GConfSources* sources,
                            const gchar* key,
                            GConfValue* value)
{
  GList* tmp;

  tmp = sources->sources;

  g_conf_clear_error();
  
  while (tmp != NULL)
    {
      GConfSource* src = tmp->data;

      if (src->flags & G_CONF_SOURCE_WRITEABLE)
        {
          /* may set error, we just leave its setting */
          g_conf_source_set_value(src, key, value);
          return;
        }
      else
        {
          /* check whether the value is set; if it is, then
             we return an error since setting an overridden value
             would have no effect
          */
          GConfValue* val;

          val = g_conf_source_query_value(tmp->data, key, NULL);
          
          if (val != NULL)
            {
              g_conf_value_destroy(val);
              g_conf_set_error(G_CONF_OVERRIDDEN,
                               _("Value for `%s' set in a read-only source at the front of your configuration path."), key);
              return;
            }
        } 

      tmp = g_list_next(tmp);
    }
}

void
g_conf_sources_unset_value   (GConfSources* sources,
                              const gchar* key)
{
  /* We unset in every layer we can write to... */
  GList* tmp;

  tmp = sources->sources;

  while (tmp != NULL)
    {
      GConfSource* src = tmp->data;

      if (src->flags & G_CONF_SOURCE_WRITEABLE)
        g_conf_source_unset_value(src, key);    /* we might pile up errors here */
      
      tmp = g_list_next(tmp);
    }
}

gboolean
g_conf_sources_dir_exists (GConfSources* sources,
                           const gchar* dir)
{
  GList *tmp;
  
  tmp = sources->sources;
  
  while (tmp != NULL) 
    {
      GConfSource* src = tmp->data;
      
      if (g_conf_source_dir_exists (src, dir)) 
        return TRUE;

      tmp = g_list_next(tmp);
    }
  
  return FALSE;
}
          
void          
g_conf_sources_remove_dir (GConfSources* sources,
                           const gchar* dir)
{
  /* We remove in every layer we can write to... */
  GList* tmp;

  tmp = sources->sources;

  while (tmp != NULL)
    {
      GConfSource* src = tmp->data;

      if (src->flags & G_CONF_SOURCE_WRITEABLE)
        g_conf_source_remove_dir(src, dir);    /* might pile up errors */
      
      tmp = g_list_next(tmp);
    }
}

void         
g_conf_sources_set_schema        (GConfSources* sources,
                                  const gchar* key,
                                  const gchar* schema_key)
{
  GList* tmp;

  tmp = sources->sources;

  while (tmp != NULL)
    {
      GConfSource* src = tmp->data;

      if (src->flags & G_CONF_SOURCE_WRITEABLE)
        {
          /* may set error, we just leave its setting */
          g_conf_source_set_schema(src, key, schema_key);
          return;
        }

      tmp = g_list_next(tmp);
    }
}

/* God, this is depressingly inefficient. Maybe there's a nicer way to
   implement it... */
/* Then we have to ship it all to the app via CORBA... */
/* Anyway, we use a hash to be sure we have a single value for 
   each key in the directory, and we always take that value from
   the first source that had one set. When we're done we flatten
   the hash.
*/
static void
hash_listify_func(gpointer key, gpointer value, gpointer user_data)
{
  GSList** list_p = user_data;

  *list_p = g_slist_prepend(*list_p, value);
}

GSList*       
g_conf_sources_all_entries   (GConfSources* sources,
                              const gchar* dir)
{
  GList* tmp;
  GHashTable* hash;
  GSList* flattened;
  gboolean first_pass = TRUE; /* as an optimization, don't bother
                                 doing hash lookups on first source
                              */

  /* As another optimization, skip the whole 
     hash thing if there's only zero or one sources
  */
  if (sources->sources == NULL)
    return NULL;

  if (sources->sources->next == NULL)
    {
      return g_conf_source_all_entries(sources->sources->data, dir);
    }

  g_assert(g_list_length(sources->sources) > 1);

  hash = g_hash_table_new(g_str_hash, g_str_equal);

  tmp = sources->sources;

  while (tmp != NULL)
    {
      GConfSource* src = tmp->data;
      GSList* pairs = g_conf_source_all_entries(src, dir);
      GSList* iter = pairs;

      while (iter != NULL)
        {
          GConfEntry* pair = iter->data;
          GConfEntry* previous;
          
          if (first_pass)
            previous = NULL; /* Can't possibly be there. */
          else
            previous = g_hash_table_lookup(hash, pair->key);
          
          if (previous != NULL)
            {
              /* Discard */
              g_conf_entry_destroy(pair);
            }
          else
            {
              /* Save */
              g_hash_table_insert(hash, pair->key, pair);
            }

          iter = g_slist_next(iter);
        }
      
      /* All pairs are either stored or destroyed. */
      g_slist_free(pairs);

      first_pass = FALSE;

      tmp = g_list_next(tmp);
    }

  flattened = NULL;

  g_hash_table_foreach(hash, hash_listify_func, &flattened);

  g_hash_table_destroy(hash);

  return flattened;
}

GSList*       
g_conf_sources_all_dirs   (GConfSources* sources,
                           const gchar* dir)
{
  GList* tmp = NULL;
  GHashTable* hash = NULL;
  GSList* flattened = NULL;
  gboolean first_pass = TRUE; /* as an optimization, don't bother
                                 doing hash lookups on first source
                              */

  g_return_val_if_fail(sources != NULL, NULL);
  g_return_val_if_fail(dir != NULL, NULL);

  /* As another optimization, skip the whole 
     hash thing if there's only zero or one sources
  */
  if (sources->sources == NULL)
    return NULL;

  if (sources->sources->next == NULL)
    {
      return g_conf_source_all_dirs(sources->sources->data, dir);
    }

  g_assert(g_list_length(sources->sources) > 1);

  hash = g_hash_table_new(g_str_hash, g_str_equal);

  tmp = sources->sources;

  while (tmp != NULL)
    {
      GConfSource* src = tmp->data;
      GSList* subdirs = g_conf_source_all_dirs(src, dir);
      GSList* iter = subdirs;

      while (iter != NULL)
        {
          gchar* subdir = iter->data;
          gchar* previous;
          
          if (first_pass)
            previous = NULL; /* Can't possibly be there. */
          else
            previous = g_hash_table_lookup(hash, subdir);
          
          if (previous != NULL)
            {
              /* Discard */
              g_free(subdir);
            }
          else
            {
              /* Save */
              g_hash_table_insert(hash, subdir, subdir);
            }

          iter = g_slist_next(iter);
        }
      
      /* All pairs are either stored or destroyed. */
      g_slist_free(subdirs);

      first_pass = FALSE;

      tmp = g_list_next(tmp);
    }

  flattened = NULL;

  g_hash_table_foreach(hash, hash_listify_func, &flattened);

  g_hash_table_destroy(hash);

  return flattened;
}

gboolean
g_conf_sources_sync_all    (GConfSources* sources)
{
  GList* tmp;
  gboolean failed = FALSE;
  
  tmp = sources->sources;

  while (tmp != NULL)
    {
      GConfSource* src = tmp->data;

      if (!g_conf_source_sync_all(src))
        failed = TRUE;

      tmp = g_list_next(tmp);
    }

  return !failed;
}
