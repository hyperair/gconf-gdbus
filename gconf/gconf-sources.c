
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

/* 
 *  Sources
 */

GConfSource* 
gconf_resolve_address(const gchar* address, GConfError** err)
{
  GConfBackend* backend;

  backend = gconf_get_backend(address, err);

  if (backend == NULL)
    return NULL;
  else
    {
      GConfSource* retval;

      retval = gconf_backend_resolve_address(backend, address, err);

      if (retval == NULL)
        {
          gconf_backend_unref(backend);
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

void         
gconf_source_destroy (GConfSource* source)
{
  GConfBackend* backend;
  
  g_return_if_fail(source != NULL);

  backend = source->backend;
  
  (*source->backend->vtable->destroy_source)(source);
  
  /* Remove ref held by the source. */
  gconf_backend_unref(backend);
}

#define SOURCE_READABLE(source, key, err)                  \
     ( ((source)->flags & GCONF_SOURCE_ALL_READABLE) ||    \
       ((source)->backend->vtable->readable != NULL &&     \
        (*(source)->backend->vtable->readable)((source), (key), (err))) )

#define SOURCE_WRITEABLE(source, key, err)                        \
     ( ((source)->flags & GCONF_SOURCE_ALL_WRITEABLE) ||          \
       ((source)->backend->vtable->writeable != NULL &&           \
        (*(source)->backend->vtable->writeable)((source), (key), (err))) )

static GConfValue*
gconf_source_query_value      (GConfSource* source,
                               const gchar* key,
                               gchar** schema_name,
                               GConfError** err)
{
  g_return_val_if_fail(source != NULL, NULL);
  g_return_val_if_fail(key != NULL, NULL);
  g_return_val_if_fail(err == NULL || *err == NULL, NULL);
  
  /* note that key validity is unchecked */

  if ( SOURCE_READABLE(source, key, err) )
    {
      g_return_val_if_fail(err == NULL || *err == NULL, NULL);
      return (*source->backend->vtable->query_value)(source, key, schema_name, err);
    }
  else
    return NULL;
}

/* return value indicates whether the key was writeable */
static gboolean
gconf_source_set_value        (GConfSource* source,
                               const gchar* key,
                               GConfValue* value,
                               GConfError** err)
{
  g_return_val_if_fail(source != NULL, FALSE);
  g_return_val_if_fail(value != NULL, FALSE);
  g_return_val_if_fail(key != NULL, FALSE);
  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);
  
  /* don't check key validity */

  if ( SOURCE_WRITEABLE(source, key, err) )
    {
      g_return_val_if_fail(err == NULL || *err == NULL, FALSE);
      (*source->backend->vtable->set_value)(source, key, value, err);
      return TRUE;
    }
  else
    return FALSE;
}

static gboolean
gconf_source_unset_value      (GConfSource* source,
                               const gchar* key,
                               GConfError** err)
{
  g_return_val_if_fail(source != NULL, FALSE);
  g_return_val_if_fail(key != NULL, FALSE);
  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);
  
  if ( SOURCE_WRITEABLE(source, key, err) )
    {
      g_return_val_if_fail(err == NULL || *err == NULL, FALSE);
      (*source->backend->vtable->unset_value)(source, key, err);
      return TRUE;
    }
  else
    return FALSE;
}

static GSList*      
gconf_source_all_entries         (GConfSource* source,
                                  const gchar* dir,
                                  GConfError** err)
{
  g_return_val_if_fail(source != NULL, NULL);
  g_return_val_if_fail(dir != NULL, NULL);
  g_return_val_if_fail(err == NULL || *err == NULL, NULL);
  
  if ( SOURCE_READABLE(source, dir, err) )
    {
      g_return_val_if_fail(err == NULL || *err == NULL, NULL);
      return (*source->backend->vtable->all_entries)(source, dir, err);
    }
  else
    return NULL;
}

static GSList*      
gconf_source_all_dirs          (GConfSource* source,
                                const gchar* dir,
                                GConfError** err)
{
  g_return_val_if_fail(source != NULL, NULL);
  g_return_val_if_fail(dir != NULL, NULL);  
  g_return_val_if_fail(err == NULL || *err == NULL, NULL);
  
  if ( SOURCE_READABLE(source, dir, err) )
    {
      g_return_val_if_fail(err == NULL || *err == NULL, NULL);
      return (*source->backend->vtable->all_subdirs)(source, dir, err);
    }
  else
    return NULL;
}

static gboolean
gconf_source_dir_exists        (GConfSource* source,
                                const gchar* dir,
                                GConfError** err)
{
  g_return_val_if_fail(source != NULL, FALSE);
  g_return_val_if_fail(dir != NULL, FALSE);
  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);
  
  if ( SOURCE_READABLE(source, dir, err) )
    {
      g_return_val_if_fail(err == NULL || *err == NULL, FALSE);
      return (*source->backend->vtable->dir_exists)(source, dir, err);
    }
  else
    return FALSE;
}

static void         
gconf_source_remove_dir        (GConfSource* source,
                                const gchar* dir,
                                GConfError** err)
{
  g_return_if_fail(source != NULL);
  g_return_if_fail(dir != NULL);
  g_return_if_fail(err == NULL || *err == NULL);
  
  if ( SOURCE_WRITEABLE(source, dir, err) )
    {
      g_return_if_fail(err == NULL || *err == NULL);
      (*source->backend->vtable->remove_dir)(source, dir, err);
    }
}

static gboolean    
gconf_source_set_schema        (GConfSource* source,
                                const gchar* key,
                                const gchar* schema_key,
                                GConfError** err)
{
  g_return_val_if_fail(source != NULL, FALSE);
  g_return_val_if_fail(key != NULL, FALSE);
  g_return_val_if_fail(schema_key != NULL, FALSE);
  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);
  
  if ( SOURCE_WRITEABLE(source, key, err) )
    {
      g_return_val_if_fail(err == NULL || *err == NULL, FALSE);
      (*source->backend->vtable->set_schema)(source, key, schema_key, err);
      return TRUE;
    }
  else
    return FALSE;
}

static gboolean
gconf_source_sync_all         (GConfSource* source, GConfError** err)
{
  return (*source->backend->vtable->sync_all)(source, err);
}

/*
 *   Source stacks
 */

GConfSources* 
gconf_sources_new_from_addresses(gchar** addresses, GConfError** err)
{
  GConfSources* sources;
  GConfError* all_errors = NULL;

  g_return_val_if_fail( (err == NULL) || (*err == NULL), NULL);
  
  sources = g_new0(GConfSources, 1);

  while (*addresses != NULL)
    {
      GConfSource* source;
      GConfError* error = NULL;
      
      source = gconf_resolve_address(*addresses, &error);

      if (source != NULL)
        {
          sources->sources = g_list_prepend(sources->sources, source);
          g_return_val_if_fail(error == NULL, NULL);
        }
      else
        {
          if (err)
            all_errors = gconf_compose_errors(all_errors, error);
          
          gconf_error_destroy(error);
        }
          
      ++addresses;
    }

  sources->sources = g_list_reverse(sources->sources);

  if (err)
    {
      g_return_val_if_fail(*err == NULL, sources);
      *err = all_errors;
    }
  
  return sources;
}

void
gconf_sources_destroy(GConfSources* sources)
{
  GList* tmp;

  tmp = sources->sources;

  while (tmp != NULL)
    {
      gconf_source_destroy(tmp->data);
      
      tmp = g_list_next(tmp);
    }

  g_list_free(sources->sources);

  g_free(sources);
}

GConfValue*   
gconf_sources_query_value (GConfSources* sources, 
                           const gchar* key,
                           GConfError** err)
{
  GList* tmp;
  gchar* schema_name = NULL;
  GConfError* error = NULL;
  
  g_return_val_if_fail(sources != NULL, NULL);
  g_return_val_if_fail(key != NULL, NULL);
  g_return_val_if_fail((err == NULL) || (*err == NULL), NULL);

  if (!gconf_key_check(key, err))
    return NULL;
  
  tmp = sources->sources;

  while (tmp != NULL)
    {
      GConfValue* val;
      GConfSource* source;

      source = tmp->data;
      
      /* we only want the first schema name we find */
      val = gconf_source_query_value(source, key,
                                     schema_name ? NULL : &schema_name, &error);

      if (error != NULL)
        {
          /* Right thing to do? Don't know. */
          g_assert(val == NULL);

          if (err)
            *err = error;
          else
            gconf_error_destroy(error);

          error = NULL;

          return NULL;
        }
      
      if (val == NULL)
        {
          ; /* keep going, try more sources */
        }
      else if (val->type == GCONF_VALUE_IGNORE_SUBSEQUENT)
        {
          /* Bail now, instead of looking for the standard values */
          gconf_value_destroy(val);
          break;
        }
      else
        return val;

      tmp = g_list_next(tmp);
    }

  g_return_val_if_fail(error == NULL, NULL);
  
  /* If we got here, there was no value; we try to look up the
     schema for this key if we have one, and use the default
     value.
  */

  if (schema_name != NULL)
    {
      GConfValue* val;

      val = gconf_sources_query_value(sources, schema_name, &error);

      if (error != NULL)
        {
          if (err)
            *err = error;
          else
            gconf_error_destroy(error);

          g_free(schema_name);
          return NULL;
        }
      else if (val != NULL &&
               val->type != GCONF_VALUE_SCHEMA)
        {
          gconf_set_error(err, GCONF_FAILED, _("Schema `%s' specified for `%s' stores a non-schema value"), schema_name, key);
                
          g_free(schema_name);

          return NULL;
        }
      else if (val != NULL)
        {
          GConfValue* retval = gconf_value_schema(val)->default_value;
          /* cheat, "unparent" the value to avoid a copy */
          gconf_value_schema(val)->default_value = NULL;
          gconf_value_destroy(val);

          g_free(schema_name);      
          
          return retval;
        }
      else
        {
          /* Schema value was not set */
          g_free(schema_name);
          
          return NULL;
        }
    }
  
  return NULL;
}

void
gconf_sources_set_value   (GConfSources* sources,
                           const gchar* key,
                           GConfValue* value,
                           GConfError** err)
{
  GList* tmp;

  g_return_if_fail(sources != NULL);
  g_return_if_fail(key != NULL);
  g_return_if_fail((err == NULL) || (*err == NULL));
  
  if (!gconf_key_check(key, err))
    return;
  
  g_assert(*key != '\0');
  
  if (key[1] == '\0')
    {
      gconf_set_error(err, GCONF_IS_DIR, _("The '/' name can only be a directory, not a key"));
      return;
    }
  
  tmp = sources->sources;

  while (tmp != NULL)
    {
      GConfSource* src = tmp->data;

      if (gconf_source_set_value(src, key, value, err))
        {
          /* source was writeable */
          return;
        }
      else
        {
          /* check whether the value is set; if it is, then
             we return an error since setting an overridden value
             would have no effect
          */
          GConfValue* val;

          val = gconf_source_query_value(tmp->data, key, NULL, NULL);
          
          if (val != NULL)
            {
              gconf_value_destroy(val);
              gconf_set_error(err, GCONF_OVERRIDDEN,
                              _("Value for `%s' set in a read-only source at the front of your configuration path."), key);
              return;
            }
        }

      tmp = g_list_next(tmp);
    }
}

void
gconf_sources_unset_value   (GConfSources* sources,
                             const gchar* key,
                             GConfError** err)
{
  /* We unset in every layer we can write to... */
  GList* tmp;
  gboolean first_writeable_reached = FALSE;
  GConfError* error = NULL;
  
  tmp = sources->sources;

  while (tmp != NULL)
    {
      GConfSource* src = tmp->data;

      if (gconf_source_unset_value(src, key, &error))
        {
          /* it was writeable */

          /* On error, set error and bail */
          if (error != NULL)
            {
              if (err)
                {
                  g_return_if_fail(*err == NULL);
                  *err = error;
                  return;
                }
              else
                {
                  gconf_error_destroy(error);
                  return;
                }
            }
          
          if (!first_writeable_reached)
            {
              /* this is the first writeable layer */
              /* FIXME set an IGNORE_SUBSEQUENT value if a read-only
                 source below us has set the value. */
              
              first_writeable_reached = TRUE;
            }
        }
      else
        {
          /* a non-writeable source; if the key is set and we haven't
             yet reached a writeable source, we need to throw an
             error. */

          /* FIXME */
        }
      
      tmp = g_list_next(tmp);
    }
}

gboolean
gconf_sources_dir_exists (GConfSources* sources,
                          const gchar* dir,
                          GConfError** err)
{
  GList *tmp;

  if (!gconf_key_check(dir, err))
    return FALSE;
  
  tmp = sources->sources;
  
  while (tmp != NULL) 
    {
      GConfSource* src = tmp->data;
      
      if (gconf_source_dir_exists (src, dir, err)) 
        return TRUE;

      tmp = g_list_next(tmp);
    }
  
  return FALSE;
}
          
void          
gconf_sources_remove_dir (GConfSources* sources,
                          const gchar* dir,
                          GConfError** err)
{
  /* We remove in every layer we can write to... */
  GList* tmp;
  
  if (!gconf_key_check(dir, err))
    return;
  
  tmp = sources->sources;

  while (tmp != NULL)
    {
      GConfSource* src = tmp->data;
      GConfError* error = NULL;
      
      gconf_source_remove_dir(src, dir, &error);

      /* On error, set error and bail */
      if (error != NULL)
        {
          if (err)
            {
              g_return_if_fail(*err == NULL);
              *err = error;
              return;
            }
          else
            {
              gconf_error_destroy(error);
              return;
            }
        }
      
      tmp = g_list_next(tmp);
    }
}

void         
gconf_sources_set_schema        (GConfSources* sources,
                                 const gchar* key,
                                 const gchar* schema_key,
                                 GConfError** err)
{
  GList* tmp;

  if (!gconf_key_check(key, err))
    return;

  if (!gconf_key_check(schema_key, err))
    return;
  
  tmp = sources->sources;

  while (tmp != NULL)
    {
      GConfSource* src = tmp->data;

      /* may set error, we just leave its setting */
      /* returns TRUE if the source was writeable */
      if (gconf_source_set_schema(src, key, schema_key, err))
        return;

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

static void
hash_destroy_entries_func(gpointer key, gpointer value, gpointer user_data)
{
  GConfEntry* entry;

  entry = value;

  gconf_entry_destroy(entry);
}

static void
hash_destroy_pointers_func(gpointer key, gpointer value, gpointer user_data)
{
  g_free(value);
}

GSList*       
gconf_sources_all_entries   (GConfSources* sources,
                             const gchar* dir,
                             GConfError** err)
{
  GList* tmp;
  GHashTable* hash;
  GSList* flattened;
  gboolean first_pass = TRUE; /* as an optimization, don't bother
                                 doing hash lookups on first source
                              */

  /* Empty GConfSources, skip it */
  if (sources->sources == NULL)
    return NULL;

  /* Only one GConfSource, just return its list of entries */
  if (sources->sources->next == NULL)
    {
      return gconf_source_all_entries(sources->sources->data, dir, err);
    }

  /* 2 or more sources in the list, use a hash to merge them */
  g_assert(g_list_length(sources->sources) > 1);

  hash = g_hash_table_new(g_str_hash, g_str_equal);

  tmp = sources->sources;

  while (tmp != NULL)
    {
      GConfSource* src;
      GSList* pairs;
      GSList* iter;
      GConfError* error = NULL;
      
      src   = tmp->data;
      pairs = gconf_source_all_entries(src, dir, &error);
      iter  = pairs;
      
      /* On error, set error and bail */
      if (error != NULL)
        {
          g_hash_table_foreach(hash, hash_destroy_entries_func, NULL);
          
          g_hash_table_destroy(hash);
          
          if (err)
            {
              g_return_val_if_fail(*err == NULL, NULL);
              *err = error;
              return NULL;
            }
          else
            {
              gconf_error_destroy(error);
              return NULL;
            }
        }

      /* Iterate over the list of entries, stuffing them
         in the hash if they're new */
      
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
              gconf_entry_destroy(pair);
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
gconf_sources_all_dirs   (GConfSources* sources,
                          const gchar* dir,
                          GConfError** err)
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
      return gconf_source_all_dirs(sources->sources->data, dir, err);
    }

  /* 2 or more sources */
  g_assert(g_list_length(sources->sources) > 1);

  hash = g_hash_table_new(g_str_hash, g_str_equal);

  tmp = sources->sources;

  while (tmp != NULL)
    {
      GConfSource* src;
      GSList* subdirs;
      GSList* iter;
      GConfError* error = NULL;
      
      src     = tmp->data;
      subdirs = gconf_source_all_dirs(src, dir, &error);
      iter    = subdirs;

      /* On error, set error and bail */
      if (error != NULL)
        {
          g_hash_table_foreach(hash, hash_destroy_pointers_func, NULL);
          
          g_hash_table_destroy(hash);
          
          if (err)
            {
              g_return_val_if_fail(*err == NULL, NULL);
              *err = error;
              return NULL;
            }
          else
            {
              gconf_error_destroy(error);
              return NULL;
            }
        }
      
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
gconf_sources_sync_all    (GConfSources* sources, GConfError** err)
{
  GList* tmp;
  gboolean failed = FALSE;
  GConfError* all_errors = NULL;
  
  tmp = sources->sources;

  while (tmp != NULL)
    {
      GConfSource* src = tmp->data;
      GConfError* error = NULL;
      
      if (!gconf_source_sync_all(src, &error))
        {
          failed = TRUE;
          g_assert(error != NULL);
        }
          
      /* On error, set error and bail */
      if (error != NULL)
        {
          if (err)
            all_errors = gconf_compose_errors(all_errors, error);

          gconf_error_destroy(error);
        }
          
      tmp = g_list_next(tmp);
    }

  if (err)
    {
      g_return_val_if_fail(*err == NULL, !failed);
      *err = all_errors;
    }
  
  return !failed;
}
