
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

#include <gnome-xml/tree.h>

#include <stdio.h>
#include <time.h>

/*
 * Overview
 * 
 * Basically we have a directory tree underneath an arbitrary root
 * directory.  The directory tree reflects the configuration
 * namespace. Each directory contains an XML file which lists the
 * subdirectories (just because it's more convenient than readdir()
 * and also avoid the problem of random directories being created by
 * users), and contains the key-value pairs underneath the current dir.
 *
 * Locking
 * 
 * Locking doesn't _really_ matter because there's only one instance
 * of the daemon at a time. However, eventually we want a non-daemon
 * command line tool and library, e.g. for the debconf stuff, 
 * so we will eventually have locking. I'll figure out then how
 * it will work.
 *
 * Caching
 *
 * I haven't decided the best way to do caching yet. As a first cut;
 * we'll cache the parse tree for any files we've looked at. The cache
 * will contain time stamps; we'll nuke cache entries that haven't been
 * used in a while, either in a main loop timeout or by checking whenever 
 * we add a new cache entry. Parse trees correspond to "directories" in the
 * configuration namespace.
 *
 * A more precise cache will store specific key-value pairs; this cache
 * will probably contain a pointer to the parse tree node the key-value
 * pair is inside.
 *
 * We'll of course need a "dirty" list of stuff not yet written to disk.
 *
 * We'll save the mod time of parse trees when we load them, so we can 
 * paranoia check that no one has change the file before we save.
 *
 * Ideally we could monitor our own process size and also free up
 * cache whenever we started to use massive RAM. However, not sure
 * this can be done at all portably. Could possibly have some measure
 * of parse tree size.
 *
 * Atomic Saving
 *
 * We'll want to save atomically by creating a temporary file for the
 * new file version, renaming the original file, moving the temporary
 * file into place, then deleting the original file, checking for
 * errors and mod times along the way.
 *     
 */

/* 
 * Cache implementations
 */ 

typedef struct _KeyCache KeyCache;

struct _KeyCache {
  GHashTable* hash;
};

typedef struct _KeyCacheEntry KeyCacheEntry;

struct _KeyCacheEntry {
  GConfValue* value;
  xmlNodePtr node;
  GTime last_access;
};

static KeyCacheEntry*
key_cache_entry_new(GConfValue* value, xmlNodePtr node)
{
  GConfValue* stored_val;
  KeyCacheEntry* entry;

  stored_val = g_conf_value_copy(value);

  /* Prime candidate for mem chunks */
  entry = g_new(KeyCacheEntry, 1);
  
  entry->node = node;
  entry->value = stored_val;
  entry->last_access = time(NULL);

  return entry;
}

static void 
key_cache_entry_destroy(KeyCacheEntry* entry)
{
  g_return_if_fail(entry != NULL);

  g_conf_value_destroy(entry->value);
  
  g_free(entry);
}

static KeyCache* 
key_cache_new(void)
{
  KeyCache* kc = g_new(KeyCache, 1);

  kc->hash = g_hash_table_new(g_str_hash, g_str_equal);

  return kc;
}

static void
foreach_destroy(gchar* key, KeyCacheEntry* entry, gpointer user_data)
{
  g_free(key);
  key_cache_entry_destroy(entry);
}

static void
key_cache_destroy(KeyCache* cache)
{
  g_hash_table_foreach(cache->hash, (GHFunc)foreach_destroy, NULL);

  g_hash_table_destroy(cache->hash);
  
  g_free(cache);
}

static guint 
key_cache_size(KeyCache* cache)
{
  return g_hash_table_size(cache->hash);
}

/* Assumes lookup has failed */
/* Returns the gchar* that actually goes in the hash table, as a
 * memory optimization the tree cache keeps this same gchar* in its
 * list of entries associated with a given parse tree 
 */
static gchar*
key_cache_add(KeyCache* kc, 
              const gchar* key, 
              xmlNodePtr node, 
              GConfValue* value)
{
  gchar* stored_key;
  KeyCacheEntry* entry;

  g_return_if_fail(kc != NULL);
  g_return_if_fail(key != NULL);
  g_return_if_fail(node != NULL);
  g_return_if_fail(value != NULL);

  stored_key = g_strdup(key);
  
  entry = key_cache_entry_new(node, value);

  g_hash_table_insert(kc->hash, stored_key, entry);
}

/* Returns a copy */
static GConfValue* 
key_cache_lookup(KeyCache* kc, const gchar* key)
{
  KeyCacheEntry* entry;

  g_return_val_if_fail(kc != NULL, NULL);
  g_return_val_if_fail(key != NULL, NULL);

  entry = g_hash_table_lookup(kc->hash, key);

  if (entry != NULL)
    {
      entry->last_access = time(NULL);
      return g_conf_value_copy(entry->value);
    }
  else 
    return NULL;
}

static void
key_cache_remove(KeyCache* kc, const gchar* key)
{
  KeyCacheEntry* entry;
  gchar* stored_key;

  g_return_val_if_fail(kc != NULL, NULL);
  g_return_val_if_fail(key != NULL, NULL);

  if (g_hash_table_lookup_extended(kc->hash, key, 
                                   &stored_key, &entry))
    {
      g_hash_table_remove(kc->hash, key);
      
      g_free(stored_key);

      key_cache_entry_destroy(entry);
      return;
    }
  else
    {
      g_warning("Attempt to remove nonexistent key cache entry");
      return;
    }
}

/*
 * Tree cache combines the key cache and XML parse tree caching
 */

/* There's some trickiness here because we have to be sure we nuke key
 * cache entries with xmlNodePtr's into a destroyed parse tree
 * whenever we destroy a parse tree 
 */

typedef struct _TreeCacheEntry TreeCacheEntry;

struct _TreeCacheEntry {
  xmlDocPtr tree;
  GSList* cached_keys;
  GTime last_access;
};

typedef struct _TreeCache TreeCache;

struct _TreeCache {
  GHashTable* trees;
  KeyCache* keys;

};

TreeCacheEntry* 
tree_cache_entry_new(xmlDocPtr tree)
{
  /* Another mem chunk use */
  TreeCacheEntry* entry;

  entry = g_new(TreeCacheEntry, 1);

  entry->tree = tree;
  entry->cached_keys = NULL;
  entry->last_access = time(NULL);

  return entry;
}

void 
tree_cache_entry_destroy(TreeCache* tc, TreeCacheEntry* entry)
{
  GSList* tmp;

  tmp = entry->cached_keys;
  while (tmp != NULL)
    {
      key_cache_remove(tc->keys, tmp->data);

      tmp = g_slist_next(tmp);
    }

  g_slist_free(entry->cached_keys);

  /* 
   * if (dirty) save tree to disk
   */

  xmlFreeDoc(entry->tree);
  
  g_free(entry);
}

static TreeCache* 
tree_cache_new(void)
{
  TreeCache* tc = g_new(TreeCache, 1);

  tc->trees = g_hash_table_new(g_str_hash, g_str_equal);
  tc->keys = key_cache_new();

  return tc;
}

static void
foreach_destroy(gchar* key, TreeCacheEntry* entry, gpointer user_data)
{
  g_free(key);
  tree_cache_entry_destroy(entry);
}

static void
tree_cache_destroy(TreeCache* cache)
{
  g_hash_table_foreach(cache->trees, (GHFunc)foreach_destroy, NULL);

  g_hash_table_destroy(cache->trees);
  
  key_cache_destroy(cache->keys);

  g_free(cache);
}

static guint 
tree_cache_size(TreeCache* cache)
{
  return g_hash_table_size(cache->trees);
}

/* Assumes lookup has failed */
static void
tree_cache_add(TreeCache* tc, 
               const gchar* dir, 
               xmlDocPtr tree)
{
  gchar* stored_dir;
  TreeCacheEntry* entry;

  g_return_if_fail(tc != NULL);
  g_return_if_fail(dir != NULL);
  g_return_if_fail(tree != NULL);

  stored_dir = g_strdup(dir);
  
  entry = tree_cache_entry_new(tree);

  g_hash_table_insert(tc->trees, stored_dir, entry);
}

static xmlDocPtr
tree_cache_lookup(TreeCache* tc, const gchar* dir)
{
  TreeCacheEntry* entry;

  g_return_val_if_fail(tc != NULL, NULL);
  g_return_val_if_fail(dir != NULL, NULL);

  entry = g_hash_table_lookup(tc->trees, dir);

  if (entry != NULL)
    {
      entry->last_access = time(NULL);
      return entry->tree;
    }
  else 
    return NULL;
}

static void
tree_cache_remove(TreeCache* tc, const gchar* dir)
{
  TreeCacheEntry* entry;
  gchar* stored_dir;

  g_return_val_if_fail(tc != NULL, NULL);
  g_return_val_if_fail(dir != NULL, NULL);

  if (g_hash_table_lookup_extended(tc->trees, dir, 
                                   &stored_dir, &entry))
    {
      g_hash_table_remove(tc->trees, dir);
      
      g_free(stored_dir);

      tree_cache_entry_destroy(entry);

      return;
    }
  else
    {
      g_warning("Attempt to remove nonexistent tree cache entry");
      return;
    }
}

static GConfValue* 
tree_cache_lookup_value(TreeCache* tc, const gchar* key)
{
  GConfValue* value;

  value = key_cache_lookup(tc->keys, key);

  if (value != NULL)
    return value;
  
  /* FIXME extract directory part, check key cache, load XML if
     necessary */
}

/*
 * XML storage implementation
 */

typedef struct _XMLSource XMLSource;

struct _XMLSource {
  GConfSource source;
  gchar* root_dir;
  TreeCache* cache;
};

static XMLSource* 
xconf_source_new(const gchar* root_dir)
{
  XMLSource* xs;

  g_return_if_fail(root_dir != NULL);

  xs = g_new0(XMLSource, 1);

  xs->root_dir = g_strdup(root_dir);

  return xs;
}

static void
xconf_source_destroy(XMLSource* source)
{
  g_return_if_fail(source != NULL);

  g_free(source->root_dir);

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



