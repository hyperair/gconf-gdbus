
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
 */

typedef struct _XMLSource XMLSource;

struct _XMLSource {
  GConfSource source;
  gchar* root_dir;
  GHashTable* trees;
  GHashTable* keys;
};

typedef struct _KeyCacheEntry KeyCacheEntry;

struct _KeyCacheEntry {
  gchar* key;   /* This string is used in multiple places, saving RAM, but 
                   the KeyCacheEntry owns it */
  GConfValue* value;  /* NULL if the key is unset */
  xmlNodePtr node;    /* NULL if the key has no value */
  GTime last_access;
};

typedef struct _TreeCacheEntry TreeCacheEntry;

struct _TreeCacheEntry {
  xmlDocPtr tree;      /* NULL if the dir doesn't exist */
  xmlNodePtr dirs;     /* Node holding list of child dirs; can be NULL */
  GSList* cached_keys; /* list of KeyCacheEntry for keys belonging to this tree */
  GTime last_access;
  guint dirty : 1;     /* Do we need to save? */
};

static KeyCacheEntry*
xs_lookup_key(XMLSource* source, const gchar* key);

static TreeCacheEntry* 
xs_lookup_dir(XMLSource* source, const gchar* dir);

static void
xs_lookup_key_and_dir(XMLSource* source, const gchar* key,
                      TreeCacheEntry** tree_entry_p, 
                      KeyCacheEntry** key_entry_p);

static XMLSource* 
xs_new(const gchar* root_dir);

static void
xs_destroy(XMLSource* source);

static void
xs_set_value(XMLSource* source, const gchar* key, GConfValue* value);

static xmlDocPtr
xs_load_dir(XMLSource* source, const gchar* dir);

static void
xs_create_new_dir(XMLSource* xsource, const gchar* dir, TreeCacheEntry* entry);

static gboolean
xs_sync_all(XMLSource* source);

static GSList* 
xs_pairs_in_dir(XMLSource* source, const gchar* dir);

static GSList*
xs_dirs_in_dir(XMLSource* source, const gchar* dir);

/*
 * Dyna-load implementation
 */

static void          shutdown        (void);

static GConfSource*  resolve_address (const gchar* address);

static GConfValue*   query_value     (GConfSource* source, const gchar* key);

static void          set_value       (GConfSource* source, const gchar* key, GConfValue* value);

static GSList*       all_entries    (GConfSource* source,
                                     const gchar* dir);

static GSList*       all_subdirs     (GConfSource* source,
                                     const gchar* dir);


static gboolean      sync_all        (GConfSource* source);

static void          destroy_source  (GConfSource* source);

static GConfBackendVTable xml_vtable = {
  shutdown,
  resolve_address,
  query_value,
  set_value,
  all_entries,
  all_subdirs,
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
      g_warning("Bad address");
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
  KeyCacheEntry* entry;

  entry = xs_lookup_key(xsource, key);

  g_assert(entry != NULL);
  
  return entry->value ? g_conf_value_copy(entry->value) : NULL;
}

static void          
set_value (GConfSource* source, const gchar* key, GConfValue* value)
{
  XMLSource* xsource = (XMLSource*)source;

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
 * XML node/doc manipulators
 */

static xmlNodePtr 
xdoc_add_entry(xmlDocPtr doc, const gchar* key, GConfValue* value);

static xmlNodePtr
xdoc_find_entry(xmlDocPtr doc, const gchar* key);

static xmlNodePtr
xdoc_find_dirs(xmlDocPtr doc);

static xmlNodePtr
xdirs_add_child_dir(xmlNodePtr node, const gchar* relative_child_dir);

static void
xentry_set_value(xmlNodePtr node, GConfValue* value);

static GConfValue*
xentry_extract_value(xmlNodePtr node);

/*
 * Cache entry functions
 */

static KeyCacheEntry*
key_cache_entry_new(const gchar* key, GConfValue* value, xmlNodePtr node);

static void
key_cache_entry_destroy(KeyCacheEntry* entry);


static TreeCacheEntry* 
tree_cache_entry_new(xmlDocPtr tree);

static void
tree_cache_entry_destroy(TreeCacheEntry* entry);

static void
tree_cache_entry_make_dirs(TreeCacheEntry* entry);

/*
 * XML source implementation
 */


static xmlDocPtr
xs_load_dir(XMLSource* source, const gchar* dir)
{
  gchar* relative;
  gchar* xmlfile;
  xmlDocPtr doc;

  relative = g_conf_concat_key_and_dir(source->root_dir, dir);

  if (!g_conf_file_test(relative, G_CONF_FILE_ISDIR))
    {
      printf("Didn't find directory `%s'\n", relative);
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
    g_warning("Failed to parse/load `%s'", xmlfile);
  
  g_free(relative);
  g_free(xmlfile);

  return doc;
}

static KeyCacheEntry*
xs_lookup_key_given_dir(XMLSource* source,
                        TreeCacheEntry* tree_entry,
                        const gchar* key,
                        /* ugliness for efficiency when making a list of all entries */
                        xmlNodePtr key_node)
{
  KeyCacheEntry* key_entry;

  g_return_val_if_fail(tree_entry != NULL, NULL);
  
  key_entry = g_hash_table_lookup(source->keys, key);
      
  if (key_entry != NULL)
    {
      tree_entry->last_access = key_entry->last_access = time(NULL);

      if (key_node && (key_entry->node != key_node))
        g_warning("Scanned key entry node doesn't match cached node!");

      return key_entry;
    }
  else 
    {
      /* Need to scan the tree, unless we were supplied with 
         a key_node 
      */
      xmlNodePtr node;
      GConfValue* value;

      if (key_node == NULL)
        node = xdoc_find_entry(tree_entry->tree, key);
      else 
        node = key_node;
     
      if (node != NULL)
        value = xentry_extract_value(node);
      else
        value = NULL;

      if (node != NULL && value == NULL)
        {
          /* Node was corrupt; no value set. */
          g_warning("Node for key `%s' has no value?", key);
          node = NULL;
        }

      /* Add key to key cache, then return the new entry. */
      key_entry = key_cache_entry_new(key, value, node); /* not that value/node are NULL if nonexistent key */
      
      /* Very important to use key_entry->key as the hash key */
      g_hash_table_insert(source->keys, key_entry->key, key_entry);
      
      /* Tree entries track associated keys,
         because the key entries hold a pointer into the XML 
         tree 
      */
      tree_entry->cached_keys = g_slist_prepend(tree_entry->cached_keys,
                                                key_entry);
      
      return key_entry;
    }
}

static void
xs_lookup_key_and_dir(XMLSource* source, const gchar* key,
                      TreeCacheEntry** tree_entry_p, 
                      KeyCacheEntry** key_entry_p)
{
  TreeCacheEntry* tree_entry;
  gchar* dir;
  KeyCacheEntry* key_entry;

  g_return_if_fail(tree_entry_p != NULL);
  g_return_if_fail(key_entry_p != NULL);

  *tree_entry_p = NULL;
  *key_entry_p = NULL;

  g_return_if_fail(source != NULL);
  g_return_if_fail(key != NULL);

  /* See if we have the directory cached in the tree cache */
  
  dir = g_conf_key_directory(key);

  g_return_if_fail(dir != NULL);

  tree_entry = xs_lookup_dir(source, dir);
 
  g_assert(tree_entry != NULL);
 
  *tree_entry_p = tree_entry;
  
  key_entry = xs_lookup_key_given_dir(source, tree_entry, key, NULL);

  g_assert(key_entry != NULL);
  
  *key_entry_p = key_entry;

  g_free(dir);
}


static KeyCacheEntry*
xs_lookup_key(XMLSource* source, const gchar* key)
{
  KeyCacheEntry* key_entry;
  TreeCacheEntry* tree_entry;
  xs_lookup_key_and_dir(source, key, &tree_entry, &key_entry);

  return key_entry;
}

static TreeCacheEntry* 
xs_lookup_dir(XMLSource* source, const gchar* dir)
{
  TreeCacheEntry* tree_entry;
  xmlDocPtr doc;

  g_return_val_if_fail(source != NULL, NULL);
  g_return_val_if_fail(dir != NULL, NULL);

  tree_entry = g_hash_table_lookup(source->trees, dir);

  if (tree_entry != NULL)
    {
      tree_entry->last_access = time(NULL);
      return tree_entry;
    }

  /* Try to load it off of disk and place in cache */
  
  doc = xs_load_dir(source, dir);

  printf("Loaded dir `%s', doc is %p\n", dir, doc);

  /* doc MAY BE NULL if the dir doesn't exist */
  
  tree_entry = tree_cache_entry_new(doc);

  g_hash_table_insert(source->trees, g_strdup(dir), tree_entry);

  return tree_entry;
}

static GSList* 
xs_pairs_in_dir(XMLSource* source, const gchar* dir)
{
  /* Our goal is to create a GConfPair for each entry in this dir */
  GSList* retval = NULL;
  TreeCacheEntry* entry;
  xmlNodePtr node;
  xmlDocPtr doc;

  entry = xs_lookup_dir(source, dir);

  if (entry == NULL)
    return retval;

  doc = entry->tree;

  if (doc == NULL ||
      doc->root == NULL ||
      doc->root->childs == NULL)
    {
      /* Empty document - just return. */
      return NULL;
    }

  if (strcmp(doc->root->name, "gconf") != 0)
    {
      g_warning("Document root isn't a <gconf> tag");
      return NULL;
    }

  node = doc->root->childs;

  while (node != NULL)
    {
      if (node->type == XML_ELEMENT_NODE && 
          (strcmp(node->name, "entry") == 0))
        {
          gchar* attr = xmlGetProp(node, "name");
          
          if (attr != NULL)
            {
              gchar* key = g_conf_concat_key_and_dir(dir, attr);
              KeyCacheEntry* key_entry;
              GConfPair* pair;

              key_entry = xs_lookup_key_given_dir(source, entry, key, node);

              g_assert(key_entry != NULL);

              pair = g_conf_pair_new(key, g_conf_value_copy(key_entry->value));

              g_assert(pair->key != NULL);
              g_assert(pair->value != NULL);

              retval = g_slist_prepend(retval, pair);
              
              free(attr);
              /* don't free key, because the GConfPair now owns it */
            }
          else
            g_warning("Entry with no name???");
        }

      node = node->next;
    }

  return retval;
}

static GSList*
xs_dirs_in_dir(XMLSource* source, const gchar* dir)
{
  GSList* retval = NULL;
  TreeCacheEntry* entry;
  xmlNodePtr node;

  printf("Listing all dirs in `%s'\n", dir);

  entry = xs_lookup_dir(source, dir);
  
  if (entry->dirs == NULL)
    {
      printf("No subdirectories in the dir entry\n");
      return NULL;
    }

  node = entry->dirs->childs;

  while (node != NULL)
    {
      gchar* attr = xmlGetProp(node, "name");      

      if (attr != NULL)
        {
          gchar* s;
          
          s = g_conf_concat_key_and_dir(dir, attr);

          free(attr);

          retval = g_slist_prepend(retval, s);
        }
      else 
        g_warning("Dir with no name???");

      node = node->next;
    }

  return retval;
}

static XMLSource* 
xs_new(const gchar* root_dir)
{
  XMLSource* xs;

  g_return_val_if_fail(root_dir != NULL, NULL);

  xs = g_new0(XMLSource, 1);

  xs->root_dir = g_strdup(root_dir);

  xs->trees = g_hash_table_new(g_str_hash, g_str_equal);

  xs->keys = g_hash_table_new(g_str_hash, g_str_equal);

  return xs;
}

static void
key_cache_foreach_destroy(gchar* key, KeyCacheEntry* entry, gpointer user_data)
{
  /* Key is stored in the entry, and destroyed with it */
  key_cache_entry_destroy(entry);
}

static void
tree_cache_foreach_destroy(gchar* key, TreeCacheEntry* entry, gpointer user_data)
{
  g_free(key);
  tree_cache_entry_destroy(entry);
}

static void
xs_destroy(XMLSource* source)
{
  g_return_if_fail(source != NULL);

  g_hash_table_foreach(source->keys, (GHFunc)key_cache_foreach_destroy, NULL);
    
  g_hash_table_destroy(source->keys);

  g_hash_table_foreach(source->trees, (GHFunc)tree_cache_foreach_destroy, NULL);

  g_hash_table_destroy(source->trees);

  g_free(source->root_dir);

  g_free(source);
}

static void
xs_set_value(XMLSource* source, const gchar* key, GConfValue* value)
{
  KeyCacheEntry* key_entry;
  TreeCacheEntry* tree_entry;

  xs_lookup_key_and_dir(source, key, &tree_entry, &key_entry);

  g_assert(tree_entry != NULL);
  g_assert(key_entry != NULL);

  if (key_entry->node != NULL)
    {
      g_return_if_fail(tree_entry != NULL);

      tree_entry->dirty = TRUE;

      xentry_set_value(key_entry->node, value);

      g_assert(key_entry->value != NULL); /* should have a value if we have a node */

      g_conf_value_destroy(key_entry->value);

      key_entry->value = g_conf_value_copy(value);

      /* last_access is set by the lookup function above. */

      return;
    }

  if (tree_entry->tree == NULL)
    {
      /* Directory doesn't exist; we need to create it then stick it in 
       * the cache. 
       */
      gchar* dir = g_conf_key_directory(key);

      g_return_if_fail(dir != NULL);

      xs_create_new_dir(source, dir, tree_entry);

      g_assert(tree_entry->tree != NULL);

      g_free(dir);  
    }
  
  g_assert(tree_entry->tree != NULL);

  /* Add the key/value to the XML tree, update it in the key cache,
     and return. 
  */

  g_assert(key_entry->node == NULL);
  g_assert(key_entry->value == NULL);

  key_entry->node = xdoc_add_entry(tree_entry->tree, key, value);
  
  key_entry->value = g_conf_value_copy(value);

  g_assert(key_entry->node != NULL);
  g_assert(key_entry->value != NULL);
}

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

static void
xs_create_new_dir(XMLSource* source, const gchar* dir, TreeCacheEntry* entry)
{
  xmlDocPtr doc = NULL;
  TreeCacheEntry* parent_entry = NULL;
  gchar* absolute;
  gchar* parent;

  absolute = g_conf_concat_key_and_dir(source->root_dir, dir);

  parent = parent_dir(dir);

  if (parent != NULL)
    {
      parent_entry = xs_lookup_dir(source, parent);

      if (parent_entry->tree == NULL)
        {
          /* Recursively create parents */
          xs_create_new_dir(source, parent, parent_entry);
        }

      g_assert(parent_entry->tree != NULL);

      g_free(parent);
    }

  if (mkdir(absolute, S_IRUSR | S_IWUSR | S_IXUSR) < 0)
    {
      if (errno == EEXIST)
        {
          if (g_conf_file_test(absolute, G_CONF_FILE_ISDIR))
            /* nothing, no problem */;
        }
      else
        g_warning("Could not make directory `%s': %s",
                  (gchar*)absolute, strerror(errno));
    }

  g_free(absolute);

  /* Create the parse tree, mark dirty so we eventually save the
     .gconf.xml file */

  doc = xmlNewDoc("1.0");

  doc->root = xmlNewDocNode(doc, NULL, "gconf", NULL);

  entry->tree = doc;

  entry->dirty = TRUE;

  g_hash_table_insert(source->trees, g_strdup(dir), entry);
  
  /* Add ourselves to the parent's directory list, unless this is the
     root directory (in which case parent_entry should be NULL) */

  if (parent_entry != NULL)
    {
      gchar* relative_dir;

      if (parent_entry->dirs == NULL)
        tree_cache_entry_make_dirs(parent_entry);

      g_assert(parent_entry->dirs != NULL);

      relative_dir = strrchr(dir, '/');

      g_assert(relative_dir != NULL);
      ++relative_dir;
      g_assert(*relative_dir != '\0'); /* would happen if the dir was '/' which it shouldn't be. */

      xdirs_add_child_dir(parent_entry->dirs, relative_dir);

      /* Mark parent to be saved */
      parent_entry->dirty = TRUE;
    }

  return;
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
      g_warning("Could not make directory `%s': %s",
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
tree_cache_foreach_sync(gchar* dir, TreeCacheEntry* entry, struct SyncData* data)
{
  gchar* filename;
  gchar* tmp_filename;
  gchar* old_filename;
  gboolean old_existed;
  gchar* relative_dir;

  /*  printf("Dir `%s' being synced; %sdirty\n", dir, entry->dirty ? "" : "not "); */

  if (!entry->dirty)
    return;

  relative_dir = g_conf_concat_key_and_dir(data->source->root_dir, dir);

  if (!create_fs_dir(relative_dir))
    {
      /* Ugh, not doing well. */
      return;
    }

  tmp_filename = g_strconcat(relative_dir, "/.gconf.xml.tmp", NULL);
  filename = g_strconcat(relative_dir, "/.gconf.xml", NULL);
  old_filename = g_strconcat(relative_dir, "/.gconf.xml.old", NULL);

  if (entry->tree == NULL)
    {
      /* We don't check errors here because the file may not exist to 
         delete, or the directory may not be empty, and neither of those
         are really bad things per se. The right thing is probably to 
         switch on errno, so FIXME sometime in the future.
      */
      unlink(filename);
      unlink(relative_dir);
      goto successful_end_of_sync;
    }
  else 
    {
      if (xmlSaveFile(tmp_filename, entry->tree) < 0)
        {
          /* I think libxml may mangle errno, but we might as well 
             try. */
          g_warning("Failed to write file `%s': %s", 
                    tmp_filename, strerror(errno));

          data->failed = TRUE;

          goto failed_end_of_sync;
        }

      old_existed = g_conf_file_exists(filename);

      if (old_existed)
        {
          if (rename(filename, old_filename) < 0)
            {
              g_warning("Failed to rename `%s' to `%s': %s",
                        filename, old_filename, strerror(errno));

              data->failed = TRUE;
              goto failed_end_of_sync;
            }
        }

      if (rename(tmp_filename, filename) < 0)
        {
          g_warning("Failed to rename `%s' to `%s': %s",
                    tmp_filename, filename, strerror(errno));

          /* Put the original file back, so this isn't a total disaster. */
          if (rename(old_filename, filename) < 0)
            {
              g_warning("Failed to restore `%s' from `%s': %s",
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
  entry->dirty = FALSE;

 failed_end_of_sync:

  g_free(old_filename);
  g_free(tmp_filename);
  g_free(filename);
}

static gboolean
xs_sync_all(XMLSource* source)
{
  struct SyncData data = { FALSE, source };

  g_hash_table_foreach(source->trees, (GHFunc)tree_cache_foreach_sync, &data);

  return !data.failed;
}

/* 
 * XML node/doc manipulators implementation
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
      g_warning("Document root isn't a <gconf> tag");
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
      g_warning("Document root isn't a <gconf> tag");
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
xdirs_add_child_dir(xmlNodePtr dirnode, const gchar* relative_child_dir)
{
  xmlNodePtr node;

  node = xmlNewChild(dirnode, NULL, "dir", NULL);
  
  xmlSetProp(node, "name", relative_child_dir);

  return node;
}

static void
xentry_set_value(xmlNodePtr node, GConfValue* value)
{
  const gchar* type;
  gchar* value_str;

  g_return_if_fail(node != NULL);
  g_return_if_fail(value != NULL);

  switch (value->type)
    {
    case G_CONF_VALUE_INT:
      type = "int";
      break;
    case G_CONF_VALUE_STRING:
      type = "string";
      break;
    case G_CONF_VALUE_FLOAT:
      type = "float";
      break;
    case G_CONF_VALUE_BOOL:
      type = "bool";
      break;
    default:
      g_assert_not_reached();
      type = NULL; /* for warnings */
      break;
    }
  
  xmlSetProp(node, "type", type);

  value_str = g_conf_value_to_string(value);
  
  xmlSetProp(node, "value", value_str);

  g_free(value_str);
}

static GConfValue*
xentry_extract_value(xmlNodePtr node)
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
  else if (strcmp(type_str, "bool") == 0)
    type = G_CONF_VALUE_BOOL;
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

/*
 * Cache entry implementations
 */

static KeyCacheEntry*
key_cache_entry_new(const gchar* key, GConfValue* value, xmlNodePtr node)
{
  GConfValue* stored_val;
  KeyCacheEntry* entry;

  g_assert((node && value) || (!node && !value));

  if (value)
    stored_val = g_conf_value_copy(value);
  else
    stored_val = NULL;

  /* Prime candidate for mem chunks */
  entry = g_new0(KeyCacheEntry, 1);
  
  entry->key = g_strdup(key);
  entry->node = node;
  entry->value = stored_val;
  entry->last_access = time(NULL);

  return entry;
}

static void
key_cache_entry_destroy(KeyCacheEntry* entry)
{
  g_return_if_fail(entry != NULL);

  g_free(entry->key);

  if (entry->value)
    g_conf_value_destroy(entry->value);
  
  g_free(entry);
}

static TreeCacheEntry* 
tree_cache_entry_new(xmlDocPtr tree)
{
  /* Another mem chunk use */
  TreeCacheEntry* entry;

  entry = g_new(TreeCacheEntry, 1);

  entry->tree = tree;
  entry->dirs = tree ? xdoc_find_dirs(tree) : NULL;
  entry->cached_keys = NULL;
  entry->last_access = time(NULL);
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

  entry->dirs = xmlNewChild(entry->tree->root, NULL, "dirs", NULL);
}

