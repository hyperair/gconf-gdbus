
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
#include <gnome-xml/parser.h>

#include <stdio.h>
#include <time.h>
#include <stdlib.h>

/*
 * Overview
 * 
 * Basically we have a directory tree underneath an arbitrary root
 * directory.  The directory tree reflects the configuration
 * namespace. Each directory contains an XML file which lists the
 * subdirectories (just because it's more convenient than readdir()
 * and also avoid the problem of random directories being created by
 * users), and contains the key-value pairs underneath the current dir.
 * The magic file in each directory is called .gconf.xml, and can't clash
 * with the database namespace because names starting with . aren't allowed.
 * So:
 * /
 *  .gconf.xml
 *   guppi/
 *     .gconf.xml
 *   gnumeric/
 *     .gconf.xml
 *
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
 * The libxml parse trees are pretty huge, so in theory we could "compress"
 * them by extracting all the information we want, then nuking the parse
 * tree. However, that would add more CPU overhead. Anyway, as a first cut
 * I'm not going to do this, we might do it later.
 *
 * Atomic Saving
 *
 * We'll want to save atomically by creating a temporary file for the
 * new file version, renaming the original file, moving the temporary
 * file into place, then deleting the original file, checking for
 * errors and mod times along the way.
 *     
 */

typedef struct _TreeCache TreeCache;

typedef struct _XMLSource XMLSource;

struct _XMLSource {
  GConfSource source;
  gchar* root_dir;
  TreeCache* cache;
};

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

typedef struct _TreeCacheEntry TreeCacheEntry;

struct _TreeCacheEntry {
  xmlDocPtr tree;
  GSList* cached_keys;
  GTime last_access;
};

struct _TreeCache {
  GHashTable* trees;
  KeyCache* keys;

};

static xmlDocPtr xconf_source_load_dir(XMLSource* source, const gchar* dir);

/*
 * Document/Node manipulators
 */

/* 
   Our XML Document has a list of child directories, and the 
   keys themselves, so:
   <gconf>
     <directories>
       <directory name="foo"/>
       <directory name="bar"/>
     </directories>
     <entry name="keyname" type="int" value="10"/>
     <entry name="keyname2" type="int" value="10"/>
   </gconf>
*/

static GConfValue*
entry_node_to_value(xmlNodePtr node)
{
  GConfValue* value;
  gchar* type_str;
  gchar* value_str;
  GConfValueType type = G_CONF_VALUE_INVALID;

  type_str = xmlGetProp(node, "type");
  value_str = xmlGetProp(node, "value");

  if (type_str == NULL || value_str == NULL)
    {
      if (type_str != NULL)
        free(type_str);
      if (value_str != NULL)
        free(value_str);
      return NULL;
    }
  
  if (strcmp(type_str, "int") == 0)
    type = G_CONF_VALUE_INT;
  else if (strcmp(type_str, "float") == 0)
    type = G_CONF_VALUE_FLOAT;
  else if (strcmp(type_str, "string") == 0)
    type = G_CONF_VALUE_STRING;
  else
    {
      g_warning("Unknown type `%s'", type_str);
      free(type_str);
      free(value_str);
      return NULL;
    }

  value = g_conf_value_new_from_string(type, value_str);

  free(value_str);
  free(type_str);

  return value;
}

static GConfValue*
doc_scan_for_value(xmlDocPtr doc, const gchar* full_key, xmlNodePtr* value_node)
{
  gchar* key;
  xmlNodePtr node;

  if (doc == NULL ||
      doc->root == NULL ||
      doc->root->childs == NULL)
    {
      /* Empty document - just return. */
      printf("Empty document\n");
      return NULL;
    }

  if (strcmp(doc->root->name, "gconf") != 0)
    {
      g_warning("Document root isn't a <gconf> tag");
      return NULL;
    }

  key = g_conf_key_key(full_key);
  
  node = doc->root->childs;

  printf("root name: %s childs name: %s\n", doc->root->name, doc->root->childs->name);

  while (node != NULL)
    {
      if (node->type == XML_ELEMENT_NODE && 
          (strcmp(node->name, "entry") == 0))
        {
          gchar* attr = xmlGetProp(node, "name");

          if (strcmp(attr, key) == 0)
            {
              /* Found it! */
              free(attr); /* free, it's from libxml */
              g_free(key);
              *value_node = node;
              return entry_node_to_value(node);
            }
          else
            {
              free(attr);
            }
        }

      node = node->next;
    }

  g_free(key);

  return NULL;
}


/* 
 * Cache implementations
 */ 

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
key_cache_foreach_destroy(gchar* key, KeyCacheEntry* entry, gpointer user_data)
{
  g_free(key);
  key_cache_entry_destroy(entry);
}

static void
key_cache_destroy(KeyCache* cache)
{
  g_hash_table_foreach(cache->hash, (GHFunc)key_cache_foreach_destroy, NULL);

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

  g_return_val_if_fail(kc != NULL, NULL);
  g_return_val_if_fail(key != NULL, NULL);
  g_return_val_if_fail(node != NULL, NULL);
  g_return_val_if_fail(value != NULL, NULL);

  stored_key = g_strdup(key);
  
  entry = key_cache_entry_new(value, node);

  g_hash_table_insert(kc->hash, stored_key, entry);

  return stored_key;
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

  g_return_if_fail(kc != NULL);
  g_return_if_fail(key != NULL);

  if (g_hash_table_lookup_extended(kc->hash, key, 
                                   (gpointer*)&stored_key, (gpointer*)&entry))
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
 * whenever we destroy a parse tree, and we have to be sure
 * we don't destroy key cache entries while tree cache entries still have 
 * them in their list of cached keys
 */

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
tree_cache_foreach_destroy(gchar* key, TreeCacheEntry* entry, TreeCache* user_data)
{
  g_free(key);
  tree_cache_entry_destroy(user_data, entry);
}

static void
tree_cache_destroy(TreeCache* cache)
{
  g_hash_table_foreach(cache->trees, (GHFunc)tree_cache_foreach_destroy, cache);

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
tree_cache_update_access_time(TreeCache* tc, const gchar* dir)
{
  TreeCacheEntry* entry;

  g_return_if_fail(tc != NULL);
  g_return_if_fail(dir != NULL);

  entry = g_hash_table_lookup(tc->trees, dir);

  if (entry != NULL)
    {
      entry->last_access = time(NULL);
    }
}

static void
tree_cache_remove(TreeCache* tc, const gchar* dir)
{
  TreeCacheEntry* entry;
  gchar* stored_dir;

  g_return_if_fail(tc != NULL);
  g_return_if_fail(dir != NULL);

  if (g_hash_table_lookup_extended(tc->trees, dir, 
                                   (gpointer*)&stored_dir, (gpointer*)&entry))
    {
      g_hash_table_remove(tc->trees, dir);
      
      g_free(stored_dir);

      tree_cache_entry_destroy(tc, entry);

      return;
    }
  else
    {
      g_warning("Attempt to remove nonexistent tree cache entry");
      return;
    }
}

static GConfValue*
tree_cache_lookup_value(TreeCache* tc, const gchar* key, XMLSource* xsource)
{
  GConfValue* value;
  gchar* dir;
  xmlDocPtr doc;
  xmlNodePtr value_node;
  TreeCacheEntry* entry;

  dir = g_conf_key_directory(key);

  printf("Directory is `%s'\n", dir);

  value = key_cache_lookup(tc->keys, key);

  if (value != NULL)
    {
      printf("Found value in key cache\n");
      tree_cache_update_access_time(tc, dir); /* mark the tree accessed, as well as the key. */
      g_free(dir);
      return value;
    }

  doc = tree_cache_lookup(tc, dir);

  if (doc == NULL)
    {
      /* Load the doc and insert in cache, if the doc exists; else
         return NULL */

      doc = xconf_source_load_dir(xsource, dir);

      if (doc == NULL)
        {
          printf("Key's directory (`%s') didn't exist\n", dir);
          return NULL;
        }

      printf("Loaded XML for directory `%s', adding to cache\n", dir);
      tree_cache_add(tc, dir, doc);
    }

  g_assert(doc != NULL);

  entry = g_hash_table_lookup(tc->trees, dir);

  if (entry == NULL)
    {
      g_warning("Document not cached properly");
      g_free(dir);
      return NULL;
    }

  g_free(dir);

  /* We have a doc in cache now, scan for the requested key and
     put the requested key in the key cache if found, else return
     NULL */
  value = doc_scan_for_value(doc, key, &value_node);
  
  if (value != NULL)
    {
      gchar* stored_key;

      g_assert(value_node != NULL);

      stored_key = key_cache_add(tc->keys, key, value_node, value);

      entry->last_access = time(NULL);
      entry->cached_keys = g_slist_prepend(entry->cached_keys, stored_key);

      printf("Found key %s\n", key);

      return value;
    }

  printf("Didn't find key `%s'\n", key);

  return NULL;
}

/*
 * XML storage implementation
 */

static XMLSource* 
xconf_source_new(const gchar* root_dir)
{
  XMLSource* xs;

  g_return_val_if_fail(root_dir != NULL, NULL);

  xs = g_new0(XMLSource, 1);

  xs->root_dir = g_strdup(root_dir);

  xs->cache = tree_cache_new();

  return xs;
}

static void
xconf_source_destroy(XMLSource* source)
{
  g_return_if_fail(source != NULL);

  g_free(source->root_dir);

  g_free(source);
}

static xmlDocPtr
xconf_source_load_dir(XMLSource* source, const gchar* dir)
{
  gchar* relative;
  gchar* xmlfile;
  xmlDocPtr doc;

  relative = g_strconcat(source->root_dir, dir, NULL);

  if (!g_conf_file_test(relative, G_CONF_FILE_ISDIR))
    {
      printf("Didn't find directory `%s'\n", relative);
      g_free(relative);
      return NULL;
    }

  xmlfile = g_strconcat(relative, "/.gconf.xml", NULL);

  if (!g_conf_file_test(xmlfile, G_CONF_FILE_ISFILE))
    {
      g_warning("Directory `%s' exists but lacks a .gconf.xml", relative);
      g_free(relative);
      g_free(xmlfile);
      return NULL;
    }
  
  doc = xmlParseFile(xmlfile);

  if (doc == NULL)
    g_warning("Failed to parse/load `%s'", xmlfile);
  
  g_free(relative);
  g_free(xmlfile);

  return doc;
}

static void
xconf_source_set_value(XMLSource* xsource, const gchar* key, GConfValue* value)
{



}

/*
 * Dyna-load implementation
 */

static void          shutdown        (void);

static GConfSource*  resolve_address (const gchar* address);

static GConfValue*   query_value     (GConfSource* source, const gchar* key);

static void          set_value       (GConfSource* source, const gchar* key, GConfValue* value);

static void          destroy_source  (GConfSource* source);

static GConfBackendVTable xml_vtable = {
  shutdown,
  resolve_address,
  query_value,
  set_value,
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
  XMLSource* xsource = (XMLSource*)source;

  return tree_cache_lookup_value(xsource->cache, key, xsource);
}

static void          
set_value (GConfSource* source, const gchar* key, GConfValue* value)
{
  XMLSource* xsource = (XMLSource*)source;

  xconf_source_set_value(xsource, key, value);
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



