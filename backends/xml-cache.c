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

#include "xml-cache.h"
#include <gconf/gconf-internals.h>

#include <time.h>

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


/* This makes hash table safer when debugging */
#ifndef GCONF_ENABLE_DEBUG
#define safe_g_hash_table_insert g_hash_table_insert
#else
static void
safe_g_hash_table_insert(GHashTable* ht, gpointer key, gpointer value)
{
  gpointer oldkey = NULL, oldval = NULL;

  if (g_hash_table_lookup_extended(ht, key, &oldkey, &oldval))
    {
      gconf_log(GCL_WARNING, "Hash key `%s' is already in the table!",
                (gchar*)key);
      return;
    }
  else
    {
      g_hash_table_insert(ht, key, value);
    }
}
#endif

static gboolean
cache_is_nonexistent(Cache* cache,
                     const gchar* key);

static void
cache_set_nonexistent   (Cache* cache,
                         const gchar* key,
                         gboolean setting);

static void
cache_insert (Cache* cache,
              Dir* d);

struct _Cache {
  gchar* root_dir;
  GHashTable* cache;
  GHashTable* nonexistent_cache;
  GSList* deleted;
  /*
    List of lists of dirs marked deleted, in the
    proper order; should be synced by deleting each
    list from front to end, starting with the first
    list.
  */ 
  
};

Cache*
cache_new (const gchar  *root_dir)
{
  Cache* cache;

  cache = g_new(Cache, 1);

  cache->root_dir = g_strdup(root_dir);

  cache->cache = g_hash_table_new(g_str_hash, g_str_equal);
  cache->nonexistent_cache = g_hash_table_new(g_str_hash, g_str_equal);

  cache->deleted = NULL;

  return cache;
}

static void cache_destroy_foreach(const gchar* key,
                                  Dir* dir, gpointer data);

static void cache_destroy_nonexistent_foreach(gchar* key,
                                              gpointer val,
                                              gpointer data);

void
cache_destroy (Cache        *cache)
{
  GSList *iter;
  
  g_free(cache->root_dir);
  g_hash_table_foreach(cache->cache, (GHFunc)cache_destroy_foreach,
                       NULL);
  g_hash_table_foreach(cache->nonexistent_cache,
                       (GHFunc)cache_destroy_nonexistent_foreach,
                       NULL);
  g_hash_table_destroy(cache->cache);
  g_hash_table_destroy(cache->nonexistent_cache);

  if (cache->deleted != NULL)
    gconf_log(GCL_WARNING, _("Unsynced directory deletions when shutting down XML backend"));
  
  iter = cache->deleted;

  while (iter != NULL)
    {
      g_slist_free(iter->data);

      iter = g_slist_next(iter);
    }
  g_slist_free(cache->deleted);
  
  g_free(cache);
}


typedef struct _SyncData SyncData;
struct _SyncData {
  gboolean failed;
  Cache* dc;
};

static void
cache_sync_foreach(const gchar* key,
                   Dir* dir,
                   SyncData* sd)
{
  GConfError* error = NULL;
  
  /* log errors but don't report the specific ones */
  if (!dir_sync(dir, &error))
    {
      sd->failed = TRUE;
      g_return_if_fail(error != NULL);
      gconf_log(GCL_ERR, error->str);
      gconf_error_destroy(error);
    }
  else
    {
      g_return_if_fail(error == NULL);
    }
}

gboolean
cache_sync       (Cache        *cache,
                  GConfError  **err)
{
  SyncData sd = { FALSE, cache };
  GSList* delete_list;

  /* First delete pending directories */
  delete_list = cache->deleted;

  while (delete_list != NULL)
    {
      GSList* tmp;

      tmp = delete_list->data;

      while (tmp != NULL)
        {
          Dir* d = tmp->data;

          if (!dir_sync(d, NULL)) /* don't get errors, they'd pile up */
            sd.failed = TRUE;
          
          tmp = g_slist_next(tmp);
        }

      g_slist_free(delete_list->data);
      
      delete_list = g_slist_next(delete_list);
    }

  g_slist_free(cache->deleted);
  cache->deleted = NULL;
  
  g_hash_table_foreach(cache->cache, (GHFunc)cache_sync_foreach,
                       &sd);

  return !sd.failed;  
}

typedef struct _CleanData CleanData;
struct _CleanData {
  GTime now;
  Cache* cache;
  GTime length;
};

static gboolean
cache_clean_foreach(const gchar* key,
                    Dir* dir, CleanData* cd)
{
  GTime last_access = dir_get_last_access(dir);

  if ((cd->now - last_access) > cd->length)
    {
      dir_destroy(dir);
      return TRUE;
    }
  else
    return FALSE;
}

void
cache_clean      (Cache        *cache,
                  GTime         older_than)
{
  CleanData cd = { 0, cache, older_than };
  
  cd.now = time(NULL); /* ha ha, it's an online store! */
  
  g_hash_table_foreach_remove(cache->cache, (GHRFunc)cache_clean_foreach,
                              &cd);
}

static void
cache_delete_dir_by_pointer(Cache* cache,
                            Dir * d,
                            GConfError** err);

static void
cache_delete_recursive(Cache* cache, Dir* d, GSList** hit_list, GConfError** err)
{  
  GSList* subdirs;
  GSList* tmp;
  gboolean failure = FALSE;
  
  subdirs = dir_all_subdirs(d, err);

  if (subdirs == NULL && err && *err != NULL)
    failure = TRUE;
  
  tmp = subdirs;
  while (tmp != NULL && !failure)
    {
      Dir* subd;
      gchar* fullkey;

      fullkey = gconf_concat_key_and_dir(dir_get_name(d), (gchar*)tmp->data);
      
      subd = cache_lookup(cache, fullkey, FALSE, err);

      g_free(tmp->data);
      g_free(fullkey);
      
      if (subd == NULL && err && *err)
        failure = TRUE;
      else if (subd != NULL &&
               !dir_is_deleted(subd))
        {
          /* recurse, whee! (unless the subdir is already deleted) */
          cache_delete_dir_by_pointer(cache, subd, err);

          if (err && *err)
            failure = TRUE;
        }
          
      tmp = g_slist_next(tmp);
    }

  g_slist_free(subdirs);
  
  /* The first directories to be deleted (fringes) go on the front
     of the list. */
  *hit_list = g_slist_prepend(*hit_list, d);
  
  /* We go ahead and mark the dir deleted */
  dir_mark_deleted(d);

  /* be sure we set error if failure occurred */
  g_return_if_fail( (!failure) || (err == NULL) || (*err != NULL));
}


static void
cache_delete_dir_by_pointer(Cache* cache,
                            Dir * d,
                            GConfError** err)
{
  GSList* hit_list = NULL;

  cache_delete_recursive(cache, d, &hit_list, err);

  /* If you first dir_cache_delete() a subdir, then dir_cache_delete()
     its parent, without syncing, first the list generated by
     the subdir delete then the list from the parent delete should
     be nuked. If you first delete a parent, then its subdir,
     really only the parent list should be nuked, but
     in effect it's OK to nuke the parent first then
     fail to nuke the subdir. So, if we prepend here,
     then nuke the list in order, it will work fine.
  */
  
  cache->deleted = g_slist_prepend(cache->deleted, hit_list);
}

void
cache_delete_dir (Cache        *cache,
                  const gchar  *key,
                  GConfError  **err)
{
  Dir* d;

  d = cache_lookup(cache, key, FALSE, err);

  if (d != NULL)
    {
      g_assert(err == NULL || *err == NULL);
      cache_delete_dir_by_pointer(cache, d, err);
    }
}

Dir*
cache_lookup     (Cache        *cache,
                  const gchar  *key,
                  gboolean create_if_missing,
                  GConfError  **err)
{
  Dir* dir;
  
  g_assert(key != NULL);

  /* Check cache */
  dir = g_hash_table_lookup(cache->cache, key);
  
  if (dir != NULL)
    {
      return dir;
    }
  else
    {
      /* Not in cache, check whether we already failed
         to load it */
      if (cache_is_nonexistent(cache, key))
        {
          if (!create_if_missing)
            return NULL;
        }
      else
        {
          /* Didn't already fail to load, try to load */
          dir = dir_load(key, cache->root_dir, err);
          
          if (dir != NULL)
            {
              g_assert(err == NULL || *err == NULL);
              
              /* Cache it */
              cache_insert(cache, dir);
              
              return dir;
            }
          else
            {
              /* Remember that we failed to load it */
              if (!create_if_missing)
                {
                  cache_set_nonexistent(cache, key, TRUE);
              
                  return NULL;
                }
              else
                {
                  if (err && *err)
                    {
                      gconf_error_destroy(*err);
                      *err = NULL;
                    }
                }
            }
        }
    }
  
  g_assert(dir == NULL);
  g_assert(create_if_missing);
  g_assert(err == NULL || *err == NULL);
  
  if (dir == NULL)
    {
      dir = dir_new(key, cache->root_dir);

      if (!dir_ensure_exists(dir, err))
        {
          dir_destroy(dir);
          
          g_return_val_if_fail((err == NULL) ||
                               (*err != NULL) ,
                               NULL);
          return NULL;
        }
      else
        cache_insert(cache, dir);
    }

  return dir;
}

static gboolean
cache_is_nonexistent(Cache* cache,
                     const gchar* key)
{
  return GPOINTER_TO_INT(g_hash_table_lookup(cache->nonexistent_cache,
                                             key));
}

static void
cache_set_nonexistent   (Cache* cache,
                         const gchar* key,
                         gboolean setting)
{
  if (setting)
    {
      /* don't use safe_ here, doesn't matter */
      g_hash_table_insert(cache->nonexistent_cache,
                          g_strdup(key),
                          GINT_TO_POINTER(TRUE));
    }
  else
    {
      gpointer origkey;
      gpointer origval;

      if (g_hash_table_lookup_extended(cache->nonexistent_cache,
                                       key,
                                       &origkey, &origval))
        {
          g_free(origkey);
          g_hash_table_remove(cache->nonexistent_cache,
                              key);
        }
    }
}

static void
cache_insert (Cache* cache,
              Dir* d)
{
  g_return_if_fail(d != NULL);
  safe_g_hash_table_insert(cache->cache, (gchar*)dir_get_name(d), d);
}

static void
cache_destroy_foreach(const gchar* key,
                      Dir* dir, gpointer data)
{
  dir_destroy(dir);
}

static void
cache_destroy_nonexistent_foreach(gchar* key,
                                  gpointer val,
                                  gpointer data)
{
  g_free(key);
}




