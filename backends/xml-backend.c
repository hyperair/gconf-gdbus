
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
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>


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
#if 0
#define safe_g_hash_table_insert g_hash_table_insert
#else
static void
safe_g_hash_table_insert(GHashTable* ht, gpointer key, gpointer value)
{
  gpointer oldkey = NULL, oldval = NULL;

  if (g_hash_table_lookup_extended(ht, key, &oldkey, &oldval))
    {
      g_warning("Hash key `%s' is already in the table!",
                (gchar*)key);
      return;
    }
  else
    {
      g_hash_table_insert(ht, key, value);
    }
}
#endif

/*
 * Overview
 * 
 * FIXME I think it might simplify this file quite a bit to rewrite it
 * with a tree instead of or in addition to a hash for the cache, so
 * you can walk the tree more easily.
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
 * The libxml parse trees are pretty huge, so in theory we could
 * "compress" them by extracting all the information we want into a
 * specialized data structure, then nuking the parse tree. However,
 * that would add more CPU overhead at load and save time. Anyway, as
 * a first cut I'm not going to do this, we might do it later.
 *
 * Atomic Saving
 *
 * We'll want to save atomically by creating a temporary file for the
 * new file version, renaming the original file, moving the temporary
 * file into place, then deleting the original file, checking for
 * errors and mod times along the way.
 *      
 * Failed lookup caching
 *
 * If a key/directory doesn't exist, we create a cache entry anyway
 * so we can rapidly re-determine that it doesn't exist.
 * We also need to save "dirty" nonexistent entries, so we can delete
 * the stuff off disk.  
 * 
 * Stupid stuff
 * 
 * CacheEntry should be an opaque datatype which hides the existence of 
 * the KeyCacheEntry and TreeCacheEntry structures, and there should be a 
 * nice Cache abstraction separate from the loading/saving mess.
 * blah. bad design, bad design.
 */

typedef struct _XMLSource XMLSource;

struct _XMLSource {
  GConfSource source;
  gchar* root_dir;
  GHashTable* cache;
};

typedef struct _CacheEntry CacheEntry;

typedef struct _KeyCacheEntry KeyCacheEntry;

struct _KeyCacheEntry {
  GConfValue* value;  /* NULL if the key is unset */
  xmlNodePtr node;    /* NULL if the key has no value */
  struct _CacheEntry* parent; /* CacheEntry with the parent tree */
};

typedef struct _TreeCacheEntry TreeCacheEntry;

struct _TreeCacheEntry {
  xmlDocPtr tree;      /* NULL if the tree hasn't been loaded yet,
                          we have just found this subdir in the 
                          parent dir */
  xmlNodePtr dirs;     /* Node holding list of child dirs; can be NULL */
  GSList* cached_keys; /* list of CacheEntry for keys holding an xmlNodePtr
                          into this tree; may not include cache entries for
                          nonexistent keys lacking an xmlNodePtr 
                       */
  guint dirty : 1;     /* Do we need to save? */
};

typedef enum {
  TREE_ENTRY,       /* Directory */
  KEY_ENTRY,        /* Key-value pair */
  DELETED_TREE_ENTRY,/* Directory that should be removed from disk */
  NONEXISTENT_ENTRY  /* Nothing */
} CacheEntryType;

struct _CacheEntry {
  CacheEntryType type;
  gchar* key;       /* This string is used in multiple places, saving RAM, but 
                       the CacheEntry owns it */
  GTime last_access;
  /* This is also in the key/tree XML, but we cache it here
     since it could change a lot and it is common to key/tree
     but stored differently
  */
  GTime mod_time;
  union {
    KeyCacheEntry* key_entry;
    TreeCacheEntry* tree_entry;
  } d;
};

static CacheEntry* cache_entry_new    (CacheEntryType type, const gchar* key);
static void        cache_entry_destroy(CacheEntry* ce);
static void        cache_entry_add_child(CacheEntry* entry,
                                         CacheEntry* child);
static void        cache_entry_remove_child(CacheEntry* entry,
                                            CacheEntry* child);

static TreeCacheEntry* tree_cache_entry_new(xmlDocPtr tree);
static void            tree_cache_entry_destroy(TreeCacheEntry* entry);
static void            tree_cache_entry_make_dirs(TreeCacheEntry* entry);

static KeyCacheEntry* key_cache_entry_new(GConfValue* value, 
                                          xmlNodePtr node);
static KeyCacheEntry* key_cache_entry_new_nocopy(GConfValue* value, 
                                                 xmlNodePtr node);
static void key_cache_entry_destroy(KeyCacheEntry* entry);

static XMLSource*  xs_new(const gchar* root_dir);
static void        xs_destroy(XMLSource* source);
static gboolean xs_check_cache(XMLSource* source, const gchar* dir, const gchar* key, CacheEntry** dir_entry, CacheEntry** key_entry);
static void xs_lookup_key_and_dir(XMLSource* source, const gchar* key, CacheEntry** dir_entry, CacheEntry** key_entry);
static CacheEntry* xs_lookup(XMLSource* source, const gchar* key);
static void        xs_slurp_dir(XMLSource* source, const gchar* dir);
static xmlDocPtr   xs_load_dir (XMLSource* source, const gchar* dir);
static xmlDocPtr   xs_entry_tree (XMLSource* source, CacheEntry* entry);
static CacheEntry* xs_cache_key(XMLSource* source, const gchar* key);
static CacheEntry* xs_new_dir_entry(XMLSource* source, const gchar* dir);
static void        xs_set_value(XMLSource* source, const gchar* key, GConfValue* value);
static void        xs_unset_value(XMLSource* source, const gchar* key);
static gboolean    xs_sync_all(XMLSource* source);
static GSList*     xs_pairs_in_dir(XMLSource* source, const gchar* dir);
static GSList*     xs_dirs_in_dir(XMLSource* source, const gchar* dir);
static void        xs_create_new_dir(XMLSource* source, CacheEntry* entry);


static xmlNodePtr  xdoc_find_entry(xmlDocPtr doc, const gchar* key);
static xmlNodePtr  xdoc_find_dirs(xmlDocPtr doc);
static xmlNodePtr  xdoc_add_entry(xmlDocPtr doc, const gchar* full_key, GConfValue* value);
static GConfValue* xentry_extract_value(xmlNodePtr node);
static void        xentry_set_value(xmlNodePtr node, GConfValue* value);
static xmlNodePtr  xdirs_find_dir(xmlNodePtr dirs, const gchar* key);
static xmlNodePtr  xdirs_add_child_dir(xmlNodePtr node, const gchar* relative_child_dir);


static gchar* parent_dir(const gchar* dir);


/*
 * Dyna-load implementation
 */

static void          shutdown        (void);

static GConfSource*  resolve_address (const gchar* address);

static GConfValue*   query_value     (GConfSource* source, const gchar* key);

static GConfMetaInfo*query_metainfo  (GConfSource* source, const gchar* key_or_dir);

static void          set_value       (GConfSource* source, const gchar* key, GConfValue* value);

static GSList*       all_entries    (GConfSource* source,
                                     const gchar* dir);

static GSList*       all_subdirs     (GConfSource* source,
                                      const gchar* dir);

static void          unset_value     (GConfSource* source,
                                      const gchar* key);

static void          remove_dir      (GConfSource* source,
                                      const gchar* dir);

static void          set_schema      (GConfSource* source,
                                      const gchar* key,
                                      const gchar* schema_key);

static gboolean      sync_all        (GConfSource* source);

static void          destroy_source  (GConfSource* source);

static GConfBackendVTable xml_vtable = {
  shutdown,
  resolve_address,
  query_value,
  query_metainfo,
  set_value,
  all_entries,
  all_subdirs,
  unset_value,
  remove_dir,
  set_schema,
  sync_all,
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
  GConfSource* source;
  guint len;

  root_dir = g_conf_address_resource(address);

  if (root_dir == NULL)
    {
      g_conf_set_error(G_CONF_BAD_ADDRESS, _("Couldn't find the XML root directory in the address"));
      return NULL;
    }

  /* Chop trailing '/' to canonicalize */
  len = strlen(root_dir);

  if (root_dir[len-1] == '/')
    root_dir[len-1] = '\0';

  /* Create the new source */

  xsource = xs_new(root_dir);

  g_free(root_dir);

  source = (GConfSource*)xsource;
  
  /* FIXME just a hack for now, eventually
     it'll be based on something 
  */
  source->flags |= G_CONF_SOURCE_WRITEABLE;

  return source;
}

static GConfValue* 
query_value (GConfSource* source, const gchar* key)
{
  XMLSource* xsource = (XMLSource*)source;
  CacheEntry* entry;

  entry = xs_lookup(xsource, key);

  g_assert(entry != NULL);
  
  switch (entry->type)
    {
    case KEY_ENTRY:
      return entry->d.key_entry->value ? 
        g_conf_value_copy(entry->d.key_entry->value) : NULL;
      break;
    case TREE_ENTRY:
      g_conf_set_error(G_CONF_IS_DIR, _("`%s' is a directory"),
                       key);
      return NULL;
      break;
    case DELETED_TREE_ENTRY: /* fall thru */
    case NONEXISTENT_ENTRY:
      return NULL;
      break;
    default:
      g_assert_not_reached();
      return NULL;
      break;
    }
}

static GConfMetaInfo*
query_metainfo  (GConfSource* source, const gchar* key_or_dir)
{
  XMLSource* xsource = (XMLSource*)source;
  CacheEntry* entry;
  GConfMetaInfo* gcmi;
  
  entry = xs_lookup(xsource, key_or_dir);

  g_assert(entry != NULL);
  
  switch (entry->type)
    {
    case TREE_ENTRY:
    case KEY_ENTRY:
      gcmi = g_conf_meta_info_new();

      gcmi->mod_time = entry->mod_time;

      /* FIXME mod user */
      
      return gcmi;
      break;
    case DELETED_TREE_ENTRY: /* fall thru */
    case NONEXISTENT_ENTRY:
      return NULL;
      break;
    default:
      g_assert_not_reached();
      return NULL;
      break;
    }
}

static void          
set_value (GConfSource* source, const gchar* key, GConfValue* value)
{
  XMLSource* xsource = (XMLSource*)source;

  g_return_if_fail(value != NULL);

  xs_set_value(xsource, key, value);
}


static GSList*             
all_entries    (GConfSource* source,
                const gchar* dir)
{
  XMLSource* xsource = (XMLSource*)source;
  
  return xs_pairs_in_dir(xsource, dir);
}

static GSList*
all_subdirs     (GConfSource* source,
                 const gchar* dir)

{  
  XMLSource* xsource = (XMLSource*)source;

  return xs_dirs_in_dir(xsource, dir);
}

static void          
unset_value     (GConfSource* source,
                 const gchar* key)
{
  XMLSource* xsource = (XMLSource*)source;

  xs_unset_value(xsource, key);
}

static void          
remove_dir      (GConfSource* source,
                 const gchar* dir)
{
  XMLSource* xsource = (XMLSource*)source;
  TreeCacheEntry* entry;

  g_warning("Not implemented");
}

static void          
set_schema      (GConfSource* source,
                 const gchar* key,
                 const gchar* schema_key)
{
  XMLSource* xsource = (XMLSource*)source;

  
  
}

static gboolean      
sync_all        (GConfSource* source)
{
  XMLSource* xsource = (XMLSource*)source;

  return xs_sync_all(xsource);
}

static void          
destroy_source  (GConfSource* source)
{
  xs_destroy((XMLSource*)source);
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

/* 
 * XMLSource implementations
 */

static XMLSource*  
xs_new(const gchar* root_dir)
{
  XMLSource* xs;

  g_return_val_if_fail(root_dir != NULL, NULL);

  xs = g_new0(XMLSource, 1);

  xs->root_dir = g_strdup(root_dir);

  xs->cache = g_hash_table_new(g_str_hash, g_str_equal);

  return xs;
}


static void
cache_foreach_destroy(gchar* key, CacheEntry* entry, gpointer user_data)
{
  /* Key is stored in the entry, and destroyed with it */
  cache_entry_destroy(entry);
}

static void
xs_destroy(XMLSource* source)
{
  g_return_if_fail(source != NULL);

  g_hash_table_foreach(source->cache, (GHFunc)cache_foreach_destroy, NULL);
    
  g_hash_table_destroy(source->cache);

  g_free(source->root_dir);

  g_free(source);
}

static gboolean
xs_check_cache(XMLSource* source,  
               const gchar* dir, 
               const gchar* key, 
               CacheEntry** dir_entry,
               CacheEntry** key_entry)
{
  if (*key_entry == NULL)
    *key_entry = g_hash_table_lookup(source->cache, key);

  if (dir != NULL && *dir_entry == NULL)
    *dir_entry = g_hash_table_lookup(source->cache, dir);
  
  if (*key_entry && *dir_entry)
    {
      (*key_entry)->last_access = (*dir_entry)->last_access = time(NULL);
      return TRUE;
    }
  else if (*key_entry && (dir == NULL))
    {
      (*key_entry)->last_access = time(NULL);
      return TRUE;
    }
  else
    return FALSE;
}

static void
xs_lookup_key_and_dir(XMLSource* source, const gchar* key, 
                      CacheEntry** dir_entry,
                      CacheEntry** key_entry)
{
  CacheEntry* entry = NULL;
  gchar* dir;

  g_return_if_fail(source != NULL);
  g_return_if_fail(key != NULL);

  *key_entry = NULL;
  *dir_entry = NULL;

  dir = parent_dir(key);

  if (xs_check_cache(source, dir, key, 
                     dir_entry, key_entry))
    return;
      
  /* We don't know if the key is a directory or 
     an entry in a directory. So we load the 
     parent directory and its contents, then check 
     whether the key is a dir or not.
  */

  /* Load all entries in the parent dir and place them
     in the cache */
  
  if (dir != NULL)
    xs_slurp_dir(source, dir);

  if (xs_check_cache(source, dir, key, 
                     dir_entry, key_entry))
    return;
  
  if (*dir_entry == NULL)
    {
      *dir_entry = cache_entry_new(NONEXISTENT_ENTRY, dir);
      safe_g_hash_table_insert(source->cache, (*dir_entry)->key, *dir_entry);
    }

  g_assert(*key_entry == NULL);
  *key_entry = cache_entry_new(NONEXISTENT_ENTRY, key);
  safe_g_hash_table_insert(source->cache, (*key_entry)->key, *key_entry);
}

static CacheEntry* 
xs_lookup(XMLSource* source, const gchar* key)
{
  CacheEntry* key_entry = NULL;
  CacheEntry* dir_entry = NULL;

  xs_lookup_key_and_dir(source, key, &dir_entry, &key_entry);

  return key_entry;
}

static CacheEntry* 
xs_new_dir_entry(XMLSource* source, const gchar* dir)
{
  CacheEntry* ce;

  ce = cache_entry_new(TREE_ENTRY, dir);
  ce->d.tree_entry = tree_cache_entry_new(NULL);

  return ce;
}

static CacheEntry*
xs_new_key_entry(XMLSource* source, CacheEntry* tree_entry,
                 xmlNodePtr enode, const gchar* key)
{
  CacheEntry* entry;
  GConfValue* value;

  g_return_val_if_fail(tree_entry != NULL, NULL);
  g_return_val_if_fail(tree_entry->type == TREE_ENTRY, NULL);
              
  value = xentry_extract_value(enode);

  if (value == NULL)
    entry = cache_entry_new(NONEXISTENT_ENTRY, key);
  else
    {
      entry = cache_entry_new(KEY_ENTRY, key);
                  
      entry->d.key_entry = key_cache_entry_new_nocopy(value, enode);

      {
        gchar* str = xmlGetProp(enode, "mtime");

        if (str == NULL)
          {
            entry->mod_time = time(NULL); /* to be synced to XML later */
          }
        else
          {
            entry->mod_time = atoi(str);
            free(str);
          }
      }
      
      cache_entry_add_child(tree_entry, entry);
    }

  return entry;
}

static xmlDocPtr
xs_load_dir(XMLSource* source, const gchar* dir)
{
  gchar* relative;
  gchar* xmlfile;
  xmlDocPtr doc;

  relative = g_conf_concat_key_and_dir(source->root_dir, dir);

  if (!g_conf_file_test(relative, G_CONF_FILE_ISDIR))
    {
      g_free(relative);
      return NULL;
    }

  xmlfile = g_strconcat(relative, "/.gconf.xml", NULL);

  if (!g_conf_file_test(xmlfile, G_CONF_FILE_ISFILE))
    {
      g_free(relative);
      g_free(xmlfile);
      return NULL;
    }
  
  doc = xmlParseFile(xmlfile);

  if (doc == NULL)
    g_conf_set_error(G_CONF_FAILED, _("Failed to parse/load `%s'"), xmlfile);
  
  g_free(relative);
  g_free(xmlfile);

  return doc;
}

static xmlDocPtr   
xs_entry_tree (XMLSource* source, CacheEntry* entry)
{
  if (entry->d.tree_entry->tree != NULL)
    return entry->d.tree_entry->tree;

  g_assert(entry->d.tree_entry->tree == NULL);

  entry->d.tree_entry->tree = xs_load_dir(source, entry->key);

  if (entry->d.tree_entry->tree == NULL)
    {
      xmlDocPtr doc;

      /* OK, loading failed for some reason. So, we create 
         the tree in memory to try to sync it later.
      */
      doc = xmlNewDoc("1.0");

      entry->d.tree_entry->tree = doc;
    }

  g_assert(entry->d.tree_entry->tree != NULL);
  
  if (entry->d.tree_entry->tree->root == NULL)
    {
      /* Fix corruption */
      entry->d.tree_entry->tree->root =
        xmlNewDocNode(entry->d.tree_entry->tree, NULL, "gconf", NULL);
    }

  g_assert(entry->d.tree_entry->tree->root != NULL);

  /* Mod time */
  {
    gchar* str;
    str = xmlGetProp(entry->d.tree_entry->tree->root, "mtime");
    if (str == NULL)
      {
        entry->mod_time = time(NULL); /* to sync later */
      }
    else
      {
        entry->mod_time = atoi(str);
        free(str);
      }
  }
  
  return entry->d.tree_entry->tree;
}

/* slurp a dir and its entries into the cache,
   non-recursively */
static void        
xs_slurp_dir(XMLSource* source, const gchar* dir)
{
  xmlDocPtr doc;
  CacheEntry* ce;

  ce = xs_cache_key(source, dir);
  
  g_assert(ce != NULL);

  if (ce->type != TREE_ENTRY)
    return;

  g_assert(ce->type == TREE_ENTRY);

  /* Iterate over subdirs */

  if (ce->d.tree_entry->dirs != NULL &&
      ce->d.tree_entry->dirs->childs != NULL)
    {
      xmlNodePtr node;

      node = ce->d.tree_entry->dirs->childs;

      while (node != NULL)
        {
          if (node->type == XML_ELEMENT_NODE && 
              (strcmp(node->name, "dir") == 0))
            {
              gchar* attr = xmlGetProp(node, "name");
              
              if (attr != NULL)
                {
                  /* Found one */
                  gchar* child;
                  CacheEntry* child_ce;
                  
                  child = g_conf_concat_key_and_dir(dir, attr);
                  
                  /* check cache */
                  
                  child_ce = g_hash_table_lookup(source->cache, child);
                  
                  /* Load if not in cache */
                  if (child_ce == NULL)
                    {
                      child_ce = xs_new_dir_entry(source, child);
                      safe_g_hash_table_insert(source->cache,
                                               child_ce->key,
                                               child_ce);
                    }

                  g_free(child);
                  free(attr); /* free, it's from libxml */
                }
              else
                {
                  g_warning("Non-dir node in the dirs node!");
                }
            }
          
          node = node->next;
        }
    }

  /* Iterate over subentries */
  
  if (xs_entry_tree(source, ce) &&
      ce->d.tree_entry->tree->root && 
      ce->d.tree_entry->tree->root->childs)
    {
      xmlNodePtr node = ce->d.tree_entry->tree->root->childs;

      while (node != NULL)
        {
          if (node->type == XML_ELEMENT_NODE && 
              (strcmp(node->name, "entry") == 0))
            {
              gchar* attr = xmlGetProp(node, "name");
              
              if (attr != NULL)
                {
                  /* Found one */
                  gchar* child;
                  CacheEntry* child_ce;

                  child = g_conf_concat_key_and_dir(dir, attr);

                  child_ce = g_hash_table_lookup(source->cache,
                                                 child);

                  if (child_ce == NULL)
                    {
                      child_ce = xs_new_key_entry(source, ce, node, child);
                      safe_g_hash_table_insert(source->cache, 
                                               child_ce->key,
                                               child_ce);
                    }

                  free(attr);
                }
              else
                {
                  g_warning("Entry with no name!");
                }
            }
          
          node = node->next;
        }
    }
}

/* Place the key and all its parent directories in the 
   cache, loading each one if necessary. Return
   the cache entry we just stored.
*/
static CacheEntry* 
xs_cache_key(XMLSource* source, const gchar* key)
{
  gchar* parent = NULL;
  CacheEntry* parent_entry = NULL;
  CacheEntry* entry = NULL;

  parent = parent_dir(key);

  if (parent != NULL)
    parent_entry = xs_cache_key(source, parent);

  g_free(parent);
  
  entry = g_hash_table_lookup(source->cache, key);

  if (entry != NULL)
    return entry;

  if (parent_entry != NULL)
    {
      /* We had a parent */
      if (parent_entry->type != TREE_ENTRY)
        {
          /* We can't possibly exist; can't be inside a non-directory. */
          entry = cache_entry_new(NONEXISTENT_ENTRY, key);
        }
      else
        {
          TreeCacheEntry* tce = NULL;
          xmlNodePtr enode = NULL;
          xmlNodePtr dnode = NULL;
          xmlDocPtr tree = NULL;

          g_assert(parent_entry->type == TREE_ENTRY);
          
          tce = parent_entry->d.tree_entry;

          tree = xs_entry_tree(source, parent_entry);
          g_assert(tree != NULL);          

          enode = xdoc_find_entry(tree, key);
          
          if (enode != NULL)
            {
              entry = xs_new_key_entry(source, parent_entry, enode, key);
            }
          else
            {
              if (tce->dirs != NULL)
                dnode = xdirs_find_dir(tce->dirs, key);

              if (dnode != NULL)
                {
                  entry = xs_new_dir_entry(source, key);
                }
              else
                {
                  entry = cache_entry_new(NONEXISTENT_ENTRY, key);
                }
            }
        }
    }
  else
    {
      /* We are "/" - no parent entry */
      g_assert(key[0] == '/' && key[1] == '\0');

      entry = xs_new_dir_entry(source, key);
    }

  g_assert(entry != NULL);

  safe_g_hash_table_insert(source->cache, entry->key, entry);

  return entry;
}

static void
xs_create_new_dir_assuming_parent_exists(XMLSource* source,
                                         CacheEntry* pce, 
                                         CacheEntry* ce)
{
  xmlDocPtr doc;
  gchar* absolute;

  g_return_if_fail(ce->type == NONEXISTENT_ENTRY ||
                   ce->type == DELETED_TREE_ENTRY);

  absolute = g_conf_concat_key_and_dir(source->root_dir, ce->key);

  if (mkdir(absolute, S_IRUSR | S_IWUSR | S_IXUSR) < 0)
    {
      if (errno == EEXIST)
        {
          if (g_conf_file_test(absolute, G_CONF_FILE_ISDIR))
            /* nothing, no problem */;
        }
      else
        g_conf_set_error(G_CONF_FAILED, _("Could not make directory `%s': %s"),
                         (gchar*)absolute, strerror(errno));       
    }

  g_free(absolute);

  /* Create the parse tree, mark dirty so we eventually save the
     .gconf.xml file */

  doc = xmlNewDoc("1.0");

  doc->root = xmlNewDocNode(doc, NULL, "gconf", NULL);

  ce->type = TREE_ENTRY;
  
  ce->d.tree_entry = tree_cache_entry_new(doc);

  ce->d.tree_entry->dirty = TRUE;
  
  /* Add ourselves to the parent's directory list, unless this is the
     root directory (in which case parent entry should be NULL) */

  if (pce != NULL)
    {
      gchar* relative_dir;

      if (pce->d.tree_entry->dirs == NULL)
        tree_cache_entry_make_dirs(pce->d.tree_entry);

      g_assert(pce->d.tree_entry->dirs != NULL);

      relative_dir = strrchr(ce->key, '/');

      g_assert(relative_dir != NULL);
      ++relative_dir;
      g_assert(*relative_dir != '\0'); /* would happen if the dir was
                                          '/' which it shouldn't be
                                          since this is the child dir */

      xdirs_add_child_dir(pce->d.tree_entry->dirs, relative_dir);

      /* Update parent mod time */
      pce->mod_time = time(NULL);
      
      /* Mark parent to be saved */
      pce->d.tree_entry->dirty = TRUE;
    }
}

static void
xs_create_new_dir(XMLSource* source, CacheEntry* entry)
{
  xmlDocPtr doc = NULL;
  CacheEntry* parent_entry = NULL;
  CacheEntry* next = NULL;
  gchar* parent;
  GSList* dir_stack = NULL;
  GSList* iter = NULL;

  g_return_if_fail(entry->type == NONEXISTENT_ENTRY ||
                   entry->type == DELETED_TREE_ENTRY);

  /* Put the dir itself and all parents in the cache */
  xs_cache_key(source, entry->key);

  /* Now build a list from top to bottom of the directories
     we need to have created 
  */
  parent = g_strdup(entry->key);
  next = entry;
  g_assert(next != NULL); /* Because of the xs_cache_key */

  while (next != NULL)
    {
      gchar* tmp;

      dir_stack = g_slist_prepend(dir_stack, next);

      tmp = parent;
      parent = parent_dir(parent);
      g_free(tmp);

      if (parent != NULL)
        {
          next = g_hash_table_lookup(source->cache, parent);
          g_assert(next != NULL);
        }
      else
        {
          g_assert(parent == NULL);
          next = NULL;
        }
    }
  
  /* Iterate over the list and be sure each 
     directory exists 
  */
  parent_entry = NULL; /* starting with root dir */
  iter = dir_stack;
  while (iter != NULL)
    {
      CacheEntry* ce;

      ce = iter->data;
      
      switch (ce->type)
        {
        case KEY_ENTRY:
          g_conf_set_error(G_CONF_IS_KEY, 
                           _("Attempt to use key `%s' as a directory"),
                           ce->key);
          goto error;      
          break;
        case NONEXISTENT_ENTRY:
        case DELETED_TREE_ENTRY:
          /* (re)create the entry */

          xs_create_new_dir_assuming_parent_exists(source, parent_entry, ce);
          break;
        case TREE_ENTRY:
          break;
        default:
          g_assert_not_reached();
          break;
        }

      iter = g_slist_next(iter);
    }

  /* We jump these assertions if there's an error */
  g_assert(entry->type == TREE_ENTRY);
  g_assert(entry->d.tree_entry != NULL);

 error:

  g_slist_free(dir_stack);
  dir_stack = NULL;

  return;
}


static void        
xs_set_value(XMLSource* source, const gchar* key, GConfValue* value)
{
  CacheEntry* key_entry;
  CacheEntry* tree_entry;

  g_assert(*key);

  xs_lookup_key_and_dir(source, key, &tree_entry, &key_entry);

  g_assert(key_entry != NULL);
  g_assert(tree_entry != NULL);

  /* Handle the fastest, simplest case first:
     key entry already exists, we just change its value 
  */
  if (key_entry->type == KEY_ENTRY)
    {
      g_assert(tree_entry->type == TREE_ENTRY);

      tree_entry->d.tree_entry->dirty = TRUE;

      if (key_entry->d.key_entry->node == NULL)
        {
          xmlDocPtr tree = xs_entry_tree(source, tree_entry);
          g_assert(tree != NULL);
          
          key_entry->d.key_entry->node = 
            xdoc_add_entry(tree, key_entry->key, value);

          /* First time we've added this entry,
             so update the mod time */
          tree_entry->mod_time = time(NULL);
        }
      else
        {
          xentry_set_value(key_entry->d.key_entry->node, value);
        }

      g_assert(key_entry->d.key_entry->value != NULL);
      g_assert(key_entry->d.key_entry->node != NULL);

      g_conf_value_destroy(key_entry->d.key_entry->value);

      key_entry->d.key_entry->value = g_conf_value_copy(value);

      /* last_access is set by the lookup function above. */

      /* Set mod time */
      key_entry->mod_time = time(NULL);
      
      return;
    }  
  else if (key_entry->type == TREE_ENTRY)
    {
      g_conf_set_error(G_CONF_IS_DIR, _("setting key value for directory `%s'"),
                       key);
      return;
    }
  
  g_assert(key_entry->type == NONEXISTENT_ENTRY || 
           key_entry->type == DELETED_TREE_ENTRY);
  

  /* Key entry doesn't exist at all; so first make sure the directory
     exists */

  switch (tree_entry->type)
    {
    case KEY_ENTRY:
      g_conf_set_error(G_CONF_IS_KEY, _("parent of `%s' is already a key"),
                       key);
      return;
      break;
    case TREE_ENTRY:
      break;
    case DELETED_TREE_ENTRY:
    case NONEXISTENT_ENTRY:
      {
        /* Bring it into existence */
        gchar* dir;

        dir = parent_dir(key);

        xs_create_new_dir(source, tree_entry);
      }
      break;
    default:
      g_assert_not_reached();
      break;
    }

  g_assert(tree_entry->type == TREE_ENTRY);

  tree_entry->d.tree_entry->dirty = TRUE;

  /* Add the key/value to the XML tree, update it in the key cache,
     and return. 
  */

  {
    xmlDocPtr tree;

    g_assert(key_entry->type == NONEXISTENT_ENTRY || 
             key_entry->type == DELETED_TREE_ENTRY);
    
    tree = xs_entry_tree(source, tree_entry);
    g_assert(tree != NULL);

    key_entry->type = KEY_ENTRY;
    key_entry->d.key_entry = 
      key_cache_entry_new(value, 
                          xdoc_add_entry(tree, 
                                         key_entry->key,
                                         value));

    /* Update mod time for both key and dir, since
       we added the key to the dir just now */
    tree_entry->mod_time = key_entry->mod_time = time(NULL);
  }

  g_assert(key_entry->type == KEY_ENTRY);
  g_assert(key_entry->d.key_entry->node != NULL);
  g_assert(key_entry->d.key_entry->value != NULL);

  g_assert(tree_entry->d.tree_entry->dirty);
  
  return;
}

static void        
xs_unset_value(XMLSource* source, const gchar* key)
{
  CacheEntry* key_entry;
  CacheEntry* tree_entry;

  g_assert(*key);

  xs_lookup_key_and_dir(source, key, &tree_entry, &key_entry);

  g_assert(key_entry != NULL);
  g_assert(tree_entry != NULL);

  switch (key_entry->type)
    {
    case NONEXISTENT_ENTRY:
    case DELETED_TREE_ENTRY:
      return; /* wasn't set */
      break;
    case TREE_ENTRY:
      g_conf_set_error(G_CONF_IS_DIR, _("Can't unset key `%s' because it's a directory"), key);
      return;
      break;
    case KEY_ENTRY:
      {
        if (key_entry->d.key_entry->value == NULL)
          return; /* already unset */
        else
          {
            xmlUnlinkNode(key_entry->d.key_entry->node);
            xmlFreeNode(key_entry->d.key_entry->node);
            
            key_entry->d.key_entry->node = NULL;
            
            g_conf_value_destroy(key_entry->d.key_entry->value);
            
            key_entry->d.key_entry->value = NULL;
            
            tree_entry->d.tree_entry->dirty = TRUE;
            
            tree_entry->mod_time = time(NULL);
            
            return;
          }
        return;
      }
      break;
    default:
      g_assert_not_reached();
      return;
      break;
    }
}

static gboolean
create_fs_dir(const gchar* dir)
{
  gchar* parent;

  if (g_conf_file_test(dir, G_CONF_FILE_ISDIR))
    return TRUE;

  parent = parent_dir(dir);

  if (parent != NULL)
    {
      if (!create_fs_dir(parent))
        {
          g_free(parent);
          return FALSE;
        }

      g_free(parent);
    }
  
  if (mkdir(dir, S_IRUSR | S_IWUSR | S_IXUSR) < 0)
    {
      g_conf_set_error(G_CONF_FAILED, _("Could not make directory `%s': %s"),
                       (gchar*)dir, strerror(errno));
      return FALSE;
    }
  else
    return TRUE;
}

struct SyncData {
  gboolean failed;
  XMLSource* source;
};

static void
cache_foreach_update_mods(const gchar* key, CacheEntry* entry,
                          const gchar* username)
{
  xmlNodePtr node = NULL;

  switch (entry->type)
    {
    case KEY_ENTRY:
      node = entry->d.key_entry->node;
      break;
    case TREE_ENTRY:
      node = entry->d.tree_entry->tree->root;
      break;
    default:
      break;
    }

  if (node != NULL)
    {
      /* Put mod time into the XML document */
      gchar* str = g_strdup_printf("%u", (guint)entry->mod_time);
      xmlSetProp(node, "mtime", str);
      g_free(str);

      xmlSetProp(node, "muser", username);
    }
}

static void
cache_foreach_sync(const gchar* key, CacheEntry* entry, struct SyncData* data)
{
  gchar* filename;
  gchar* tmp_filename;
  gchar* old_filename;
  gboolean old_existed;
  gchar* relative_dir;

  /*  printf("Dir `%s' being synced; %sdirty\n", dir, entry->dirty ? "" : "not "); */

  if (entry->type == KEY_ENTRY ||
      entry->type == NONEXISTENT_ENTRY)
    return;

  if (entry->type == TREE_ENTRY && 
      !entry->d.tree_entry->dirty)
    return;

  relative_dir = g_conf_concat_key_and_dir(data->source->root_dir, key);

  if (!create_fs_dir(relative_dir))
    {
      /* Ugh, not doing well. */
      return;
    }

  tmp_filename = g_strconcat(relative_dir, "/.gconf.xml.tmp", NULL);
  filename = g_strconcat(relative_dir, "/.gconf.xml", NULL);
  old_filename = g_strconcat(relative_dir, "/.gconf.xml.old", NULL);

  if (entry->type == DELETED_TREE_ENTRY)
    {
      /* We don't check errors here because the file may not exist to 
         delete, or the directory may not be empty, and neither of those
         are really bad things per se. The right thing is probably to 
         switch on errno, so FIXME sometime in the future.
      */

      /* FIXME broken because we need to delete child dirs 
         before parents, so each sync will only manage
         to delete the tree fringes, oops.
      */

      unlink(filename);
      unlink(relative_dir);

      /* No longer a deleted tree, just an absent key */
      entry->type = NONEXISTENT_ENTRY;

      goto successful_end_of_sync;
    }
  else 
    {
      g_assert(entry->type == TREE_ENTRY);
      
      if (xmlSaveFile(tmp_filename, entry->d.tree_entry->tree) < 0)
        {
          /* I think libxml may mangle errno, but we might as well 
             try. */
          g_conf_set_error(G_CONF_FAILED, _("Failed to write file `%s': %s"), 
                           tmp_filename, strerror(errno));

          data->failed = TRUE;

          goto failed_end_of_sync;
        }

      old_existed = g_conf_file_exists(filename);

      if (old_existed)
        {
          if (rename(filename, old_filename) < 0)
            {
              g_conf_set_error(G_CONF_FAILED, 
                               _("Failed to rename `%s' to `%s': %s"),
                               filename, old_filename, strerror(errno));

              data->failed = TRUE;
              goto failed_end_of_sync;
            }
        }

      if (rename(tmp_filename, filename) < 0)
        {
          g_conf_set_error(G_CONF_FAILED, _("Failed to rename `%s' to `%s': %s"),
                           tmp_filename, filename, strerror(errno));

          /* Put the original file back, so this isn't a total disaster. */
          if (rename(old_filename, filename) < 0)
            {
              g_conf_set_error(G_CONF_FAILED, _("Failed to restore `%s' from `%s': %s"),
                               filename, old_filename, strerror(errno));
            }

          data->failed = TRUE;
          goto failed_end_of_sync;
        }

      if (old_existed)
        {
          if (unlink(old_filename) < 0)
            {
              g_warning("Failed to delete old file `%s': %s",
                        old_filename, strerror(errno));
              /* Not a failure, just leaves cruft around. */
            }
        }
    }

 successful_end_of_sync:
  
  /* All successful, mark it not-dirty-anymore */
  entry->d.tree_entry->dirty = FALSE;

 failed_end_of_sync:

  g_free(old_filename);
  g_free(tmp_filename);
  g_free(filename);
}

static gboolean    
xs_sync_all(XMLSource* source)
{
  struct SyncData data = { FALSE, source };  

  g_hash_table_foreach(source->cache, (GHFunc)cache_foreach_update_mods,
                       g_get_user_name());
  g_hash_table_foreach(source->cache, (GHFunc)cache_foreach_sync, &data);

  return !data.failed;
}

static GSList*     
xs_pairs_in_dir(XMLSource* source, const gchar* dir)
{

  return NULL;
}

static GSList*     
xs_dirs_in_dir(XMLSource* source, const gchar* dir)
{

  return NULL;
}


/*
 * XML document foolishness 
 */

static xmlNodePtr 
xdoc_add_entry(xmlDocPtr doc, const gchar* full_key, GConfValue* value)
{
  xmlNodePtr node;
  gchar* key;

  key = g_conf_key_key(full_key);

  node = xmlNewChild(doc->root, NULL, "entry", NULL);

  xmlSetProp(node, "name", key);

  g_free(key);
  
  xentry_set_value(node, value);

  return node;
}

static xmlNodePtr
xdoc_find_entry(xmlDocPtr doc, const gchar* full_key)
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
      g_conf_set_error(G_CONF_FAILED, _("Document root isn't a <gconf> tag"));
      return NULL;
    }

  key = g_conf_key_key(full_key);
  
  node = doc->root->childs;

  while (node != NULL)
    {
      if (node->type == XML_ELEMENT_NODE && 
          (strcmp(node->name, "entry") == 0))
        {
          gchar* attr = xmlGetProp(node, "name");

          if (attr != NULL)
            {
              if (strcmp(attr, key) == 0)
                {
                  /* Found it! */
                  free(attr); /* free, it's from libxml */
                  g_free(key);
                  return node;
                }
              else
                {
                  free(attr);
                }
            }
          else
            {
              g_warning("Entry with no name!");
            }
        }

      node = node->next;
    }

  g_free(key);

  return NULL;
}

static xmlNodePtr
xdoc_find_dirs(xmlDocPtr doc)
{
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
      g_conf_set_error(G_CONF_FAILED, _("Document root isn't a <gconf> tag"));
      return NULL;
    }
  
  node = doc->root->childs;

  while (node != NULL)
    {
      if (node->type == XML_ELEMENT_NODE && 
          (strcmp(node->name, "dirs") == 0))
        {
          return node;
        }

      node = node->next;
    }

  return NULL;
}

static xmlNodePtr
xdirs_find_dir(xmlNodePtr dirs, const gchar* full_key)
{
  gchar* key;
  xmlNodePtr node;

  if (dirs == NULL ||
      dirs->childs == NULL)
    return NULL;

  key = g_conf_key_key(full_key);
  
  node = dirs->childs;

  while (node != NULL)
    {
      if (node->type == XML_ELEMENT_NODE && 
          (strcmp(node->name, "dir") == 0))
        {
          gchar* attr = xmlGetProp(node, "name");

          if (attr != NULL)
            {
              if (strcmp(attr, key) == 0)
                {
                  /* Found it! */
                  free(attr); /* free, it's from libxml */
                  g_free(key);
                  return node;
                }
              else
                {
                  free(attr);
                }
            }
          else
            {
              g_warning("Non-dir node in the dirs node!");
            }
        }

      node = node->next;
    }

  g_free(key);

  return NULL;
}

static xmlNodePtr
xdirs_add_child_dir(xmlNodePtr dirnode, const gchar* relative_child_dir)
{
  xmlNodePtr node;

  node = xmlNewChild(dirnode, NULL, "dir", NULL);
  
  xmlSetProp(node, "name", relative_child_dir);

  return node;
}

static GConfValue*
xentry_extract_value(xmlNodePtr node)
{
  GConfValue* value;
  gchar* type_str;
  GConfValueType type = G_CONF_VALUE_INVALID;

  type_str = xmlGetProp(node, "type");

  type = g_conf_value_type_from_string(type_str);

  if (type == G_CONF_VALUE_INVALID)
    {
      g_warning("Unknown type `%s'", type_str);
      free(type_str);
      return NULL;
    }

  if (type != G_CONF_VALUE_SCHEMA)
    {
      gchar* value_str;

      value_str = xmlGetProp(node, "value");
      
      if (type_str == NULL || value_str == NULL)
        {
          if (type_str != NULL)
            free(type_str);
          if (value_str != NULL)
            free(value_str);
          return NULL;
        }

      value = g_conf_value_new_from_string(type, value_str);

      free(value_str);
      free(type_str);

      return value;
    }
  else
    {
      gchar* sd_str;
      gchar* ld_str;
      gchar* owner_str;
      gchar* stype_str;
      GConfSchema* sc;
      GConfValue* value;

      free(type_str);

      sd_str = xmlGetProp(node, "short_desc");
      owner_str = xmlGetProp(node, "owner");
      stype_str = xmlGetProp(node, "stype");
      ld_str = xmlNodeGetContent(node);

      sc = g_conf_schema_new();

      if (sd_str)
        {
          g_conf_schema_set_short_desc(sc, sd_str);
          free(sd_str);
        }
      if (ld_str)
        {
          g_conf_schema_set_long_desc(sc, ld_str);
          free(ld_str);
        }
      if (owner_str)
        {
          g_conf_schema_set_owner(sc, owner_str);
          free(owner_str);
        }
      if (stype_str)
        {
          GConfValueType stype;
          stype = g_conf_value_type_from_string(stype_str);
          g_conf_schema_set_type(sc, stype);
          free(stype_str);
        }

      value = g_conf_value_new(G_CONF_VALUE_SCHEMA);
      
      g_conf_value_set_schema_nocopy(value, sc);

      return value;
    }
}

static void
xentry_set_value(xmlNodePtr node, GConfValue* value)
{
  const gchar* type;
  gchar* value_str;

  g_return_if_fail(node != NULL);
  g_return_if_fail(value != NULL);

  type = g_conf_value_type_to_string(value->type);
  
  xmlSetProp(node, "type", type);

  if (value->type != G_CONF_VALUE_SCHEMA)
    {
      value_str = g_conf_value_to_string(value);
  
      xmlSetProp(node, "value", value_str);

      g_free(value_str);
    }
  else
    {
      GConfSchema* sc = g_conf_value_schema(value);

      xmlSetProp(node, "value", NULL);
      xmlSetProp(node, "stype", g_conf_value_type_to_string(sc->type));
      /* OK if these are set to NULL, since that unsets the property */
      xmlSetProp(node, "short_desc", sc->short_desc);
      xmlSetProp(node, "owner", sc->owner);
      xmlNodeSetContent(node, sc->long_desc);
    }
}

/*
 * Cache entry implementation 
 */

CacheEntry* 
cache_entry_new    (CacheEntryType type, const gchar* key)
{
  CacheEntry* ce;

  ce = g_new0(CacheEntry, 1);
  
  ce->type = type;
  ce->key = g_strdup(key);
  ce->last_access = time(NULL);

  return ce;
}

void        
cache_entry_destroy(CacheEntry* ce)
{
  switch (ce->type) {
  case TREE_ENTRY:
    g_assert(ce->d.tree_entry != NULL);
    tree_cache_entry_destroy(ce->d.tree_entry);
    break;
  case KEY_ENTRY:
    g_assert(ce->d.key_entry != NULL);
    key_cache_entry_destroy(ce->d.key_entry);
    break;
  default:
    break;
  }

  g_free(ce->key);
}

static void
cache_entry_add_child(CacheEntry* entry,
                      CacheEntry* child)
{
  g_return_if_fail(entry->type == TREE_ENTRY);
  g_return_if_fail(child->type == KEY_ENTRY);
  g_return_if_fail(child->d.key_entry->node != NULL);
  /* since we have a node */
  g_return_if_fail(entry->d.tree_entry->tree != NULL);
  g_return_if_fail(child->d.key_entry->node->doc == entry->d.tree_entry->tree);
  g_return_if_fail(child->d.key_entry->parent == NULL);

  entry->d.tree_entry->cached_keys = 
    g_slist_prepend(entry->d.tree_entry->cached_keys, child);

  child->d.key_entry->parent = entry;
}

static void
cache_entry_remove_child(CacheEntry* entry,
                         CacheEntry* child)
{
  g_return_if_fail(entry->type == TREE_ENTRY);
  g_return_if_fail(child->type == KEY_ENTRY);
  g_return_if_fail(child->d.key_entry->node != NULL);
  g_return_if_fail(entry->d.tree_entry->tree != NULL); /* since we have a node */
  g_return_if_fail(child->d.key_entry->node->doc == entry->d.tree_entry->tree);
  g_return_if_fail(child->d.key_entry->parent != NULL);
  g_return_if_fail(child->d.key_entry->parent == entry);

  entry->d.tree_entry->cached_keys = 
    g_slist_remove(entry->d.tree_entry->cached_keys, child);
  
  child->d.key_entry->parent = NULL;

  /* Can't keep this reference; in effect this means that the 
     key entry is now useless, and should be nuked
  */
  child->d.key_entry->node = NULL;
}

/* 
 * Tree entry impl
 */

static TreeCacheEntry* 
tree_cache_entry_new(xmlDocPtr tree)
{
  /* Another mem chunk use */
  TreeCacheEntry* entry;

  entry = g_new(TreeCacheEntry, 1);

  entry->tree = tree;
  entry->dirs = tree ? xdoc_find_dirs(tree) : NULL;
  entry->cached_keys = NULL;
  entry->dirty = FALSE;

  return entry;
}

static void
tree_cache_entry_destroy(TreeCacheEntry* entry)
{
  g_slist_free(entry->cached_keys);

  if (entry->tree)
    xmlFreeDoc(entry->tree);
  
  g_free(entry);
}


static void
tree_cache_entry_make_dirs(TreeCacheEntry* entry)
{
  g_return_if_fail(entry->dirs == NULL);
  g_return_if_fail(entry->tree != NULL);
  g_return_if_fail(entry->tree->root != NULL);

  entry->dirs = xmlNewChild(entry->tree->root, NULL, "dirs", NULL);
}

/* 
 * Key entry impl
 */

static KeyCacheEntry* 
key_cache_entry_new_nocopy(GConfValue* value, xmlNodePtr node)
{
  KeyCacheEntry* entry;

  g_assert((node && value) || (!node && !value));

  /* Prime candidate for mem chunks */
  entry = g_new0(KeyCacheEntry, 1);
  
  entry->node = node;
  entry->value = value;

  return entry;
}

static KeyCacheEntry* 
key_cache_entry_new(GConfValue* value, xmlNodePtr node)
{
  GConfValue* stored_val;

  g_assert((node && value) || (!node && !value));

  if (value)
    stored_val = g_conf_value_copy(value);
  else
    stored_val = NULL;
  
  return key_cache_entry_new_nocopy(stored_val, node);
}

static void 
key_cache_entry_destroy(KeyCacheEntry* entry)
{
  g_return_if_fail(entry != NULL);

  if (entry->value)
    g_conf_value_destroy(entry->value);
  
  g_free(entry);
}


/*
 * Cruft
 */

static gchar* 
parent_dir(const gchar* dir)
{
  /* We assume the dir doesn't have a trailing slash, since that's our
     standard canonicalization in GConf */
  gchar* parent;
  gchar* last_slash;

  g_return_val_if_fail(*dir != '\0', NULL);

  if (dir[1] == '\0')
    {
      g_assert(dir[0] == '/');
      return NULL;
    }

  parent = g_strdup(dir);

  last_slash = strrchr(parent, '/');

  /* dir must have had at least the root slash in it */
  g_assert(last_slash != NULL);
  
  if (last_slash != parent)
    *last_slash = '\0';
  else 
    {
      ++last_slash;
      *last_slash = '\0';
    }

  return parent;
}
