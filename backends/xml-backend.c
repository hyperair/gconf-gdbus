
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
#include <gconf/gconfd-error.h>
#include <gconf/gconf.h>

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
#include <dirent.h>
#include <limits.h>

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
      gconf_log(GCL_DEBUG, "Hash key `%s' is already in the table!",
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
 * Basically we have a directory tree underneath an arbitrary root
 * directory.  The directory tree reflects the configuration
 * namespace. Each directory contains an XML file which contains
 * metadata for the directory and the key-value pairs in that
 * directory.  The magic file in each directory is called .gconf.xml,
 * and can't clash with the database namespace because names starting
 * with . aren't allowed.  So:
 *
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
 */

/** Misc */

static gchar* parent_dir(const gchar* dir);

typedef struct _XMLSource XMLSource;

/** Dir **/

typedef struct _Dir Dir;

struct _Dir {
  XMLSource* source;
  gchar* key;
  gchar* fs_dirname;
  gchar* xml_filename;
  GTime last_access; /* so we know when to un-cache */
  xmlDocPtr doc;
  GHashTable* entry_cache; /* store key-value entries */
  GHashTable* subdir_cache; /* store subdirectories */
  guint dirty : 1;
  guint deleted : 1;
};

/* returns NULL if the load fails. */
static Dir*        dir_load        (XMLSource* source, const gchar* key);
static Dir*        dir_create      (XMLSource* source, const gchar* key);
static void        dir_destroy     (Dir* d);
static gboolean    dir_sync        (Dir* d);
 /* key should have no slashes in it */
static void        dir_set_value   (Dir* d, const gchar* relative_key, GConfValue* value);
static GConfValue* dir_get_value   (Dir* d, const gchar* relative_key,
                                    gchar** schema_name);
static GConfMetaInfo* dir_get_metainfo(Dir* d, const gchar* relative_key);
static void        dir_unset_value (Dir* d, const gchar* relative_key);
static GSList*     dir_all_entries (Dir* d);
static GSList*     dir_all_subdirs (Dir* d);
static void        dir_set_schema  (Dir* d,
                                    const gchar* relative_key,
                                    const gchar* schema_key);
/* Marks for deletion; dir cache really has to implement this,
   since it is recursive */
static void        dir_delete      (Dir* d);
static GTime       dir_last_access (Dir* d);
static gboolean    dir_deleted     (Dir* d);

/** DirCache **/

typedef struct _DirCache DirCache;

struct _DirCache {
  XMLSource* source;
  GHashTable* cache;
  GHashTable* nonexistent_cache;
  GSList* deleted; /* List of lists of dirs marked deleted, in the
                      proper order; should be synced by deleting each
                      list from front to end, starting with the first
                      list.
                   */
  GTime length; /* amount of time to keep cached items */
};

static DirCache*   dir_cache_new         (XMLSource* source, GTime length);
static void        dir_cache_destroy     (DirCache* dc);
static Dir*        dir_cache_lookup      (DirCache* dc,
                                          const gchar* key);
static gboolean    dir_cache_lookup_nonexistent(DirCache* dc,
                                                const gchar* key);
static void        dir_cache_set_nonexistent   (DirCache* dc,
                                                const gchar* key,
                                                gboolean setting);
static void        dir_cache_insert      (DirCache* dc, Dir* d);
static void        dir_cache_remove      (DirCache* dc, Dir* d);
static gboolean    dir_cache_sync        (DirCache* dc);
static void        dir_cache_clean       (DirCache* dc);
static void        dir_cache_delete      (DirCache* dc, Dir* d);

static Dir*       dir_cache_do_very_best_to_load_dir(DirCache* dc,
                                                     const gchar* key);
static Dir*       dir_cache_create_or_load_dir      (DirCache* dc,
                                                     const gchar* key);

/** XMLSource **/

struct _XMLSource {
  GConfSource source;
  gchar* root_dir;
  DirCache* cache;
  guint timeout_id;
};

static XMLSource* xs_new       (const gchar* root_dir);
static void       xs_destroy   (XMLSource* source);

/*
 * VTable functions
 */

static void          x_shutdown        (void); /* shutdown() is a BSD libc function */

static GConfSource*  resolve_address (const gchar* address);

static GConfValue*   query_value     (GConfSource* source, const gchar* key, gchar** schema_name);

static GConfMetaInfo*query_metainfo  (GConfSource* source, const gchar* key);

static void          set_value       (GConfSource* source, const gchar* key, GConfValue* value);
static GSList*       all_entries    (GConfSource* source,
                                     const gchar* dir);

static GSList*       all_subdirs     (GConfSource* source,
                                      const gchar* dir);

static void          unset_value     (GConfSource* source,
                                      const gchar* key);
static gboolean      dir_exists      (GConfSource *source,
                                      const gchar *dir);
static void          remove_dir      (GConfSource* source,
                                      const gchar* dir);

static void          set_schema      (GConfSource* source,
                                      const gchar* key,
                                      const gchar* schema_key);

static gboolean      sync_all        (GConfSource* source);

static void          destroy_source  (GConfSource* source);

static GConfBackendVTable xml_vtable = {
  x_shutdown,
  resolve_address,
  query_value,
  query_metainfo,
  set_value,
  all_entries,
  all_subdirs,
  unset_value,
  dir_exists,
  remove_dir,
  set_schema,
  sync_all,
  destroy_source
};

static void          
x_shutdown (void)
{
  gconf_log(GCL_INFO, _("Unloading XML backend module."));
}

static GConfSource*  
resolve_address (const gchar* address)
{
  gchar* root_dir;
  XMLSource* xsource;
  GConfSource* source;
  guint len;

  root_dir = gconf_address_resource(address);

  if (root_dir == NULL)
    {
      gconf_set_error(GCONF_BAD_ADDRESS, _("Couldn't find the XML root directory in the address"));
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
  source->flags |= GCONF_SOURCE_WRITEABLE;
  
  return source;
}

static GConfValue* 
query_value (GConfSource* source, const gchar* key, gchar** schema_name)
{
  XMLSource* xs = (XMLSource*)source;
  gchar* parent;
  Dir* dir;

  parent = gconf_key_directory(key);
  
  g_assert(parent != NULL);
  
  dir = dir_cache_do_very_best_to_load_dir(xs->cache, parent);

  g_free(parent);
  parent = NULL;
  
  if (dir != NULL)
    {
      const gchar* relative_key;
  
      relative_key = gconf_key_key(key);

      return dir_get_value(dir, relative_key, schema_name);
    }
  else
    return NULL;
}

static GConfMetaInfo*
query_metainfo  (GConfSource* source, const gchar* key)
{
  XMLSource* xs = (XMLSource*)source;
  gchar* parent;
  Dir* dir;

  parent = gconf_key_directory(key);

  if (parent != NULL)
    {
      dir = dir_cache_do_very_best_to_load_dir(xs->cache, parent);
      g_free(parent);
      parent = NULL;
      
      if (dir != NULL)
        return dir_get_metainfo(dir, key);
    }

  /* No metainfo found */
  return NULL;
}

static void          
set_value (GConfSource* source, const gchar* key, GConfValue* value)
{
  XMLSource* xs = (XMLSource*)source;
  Dir* dir;
  gchar* parent;
  
  g_return_if_fail(value != NULL);
  g_return_if_fail(source->flags & GCONF_SOURCE_WRITEABLE);
  
  parent = gconf_key_directory(key);
  
  g_assert(parent != NULL);
  
  dir = dir_cache_create_or_load_dir(xs->cache, parent);
  
  g_free(parent);
  parent = NULL;

  if (dir == NULL)
    return; /* error should be set */
  else
    {
      const gchar* relative_key;
      
      relative_key = gconf_key_key(key);
      
      dir_set_value(dir, relative_key, value);
    }
}


static GSList*             
all_entries    (GConfSource* source,
                const gchar* key)
{
  XMLSource* xs = (XMLSource*)source;
  Dir* dir;

  dir = dir_cache_do_very_best_to_load_dir(xs->cache, key);
  
  if (dir == NULL)
    return NULL;
  else
    return dir_all_entries(dir);
}

static GSList*
all_subdirs     (GConfSource* source,
                 const gchar* key)

{
  Dir* dir;
  XMLSource* xs = (XMLSource*)source;

  dir = dir_cache_do_very_best_to_load_dir(xs->cache, key);
  
  if (dir == NULL)
    return NULL;
  else
    return dir_all_subdirs(dir);
}

static void          
unset_value     (GConfSource* source,
                 const gchar* key)
{
  XMLSource* xs = (XMLSource*)source;
  Dir* dir;
  gchar* parent;

  gconf_log(GCL_DEBUG, "XML backend: unset value `%s'", key);
  
  parent = gconf_key_directory(key);
  
  dir = dir_cache_do_very_best_to_load_dir(xs->cache, parent);

  g_free(parent);
  
  if (dir == NULL)
    return;
  else
    {
      const gchar* relative_key;
  
      relative_key = gconf_key_key(key);

      dir_unset_value(dir, relative_key);
    }
}

static gboolean
dir_exists      (GConfSource*source,
                 const gchar* key)
{
  XMLSource *xs = (XMLSource*)source;
  Dir* dir;
  
  dir = dir_cache_do_very_best_to_load_dir(xs->cache, key);
  
  return (dir != NULL);
}  

static void          
remove_dir      (GConfSource* source,
                 const gchar* key)
{
  XMLSource* xs = (XMLSource*)source;
  Dir* dir;
  
  dir = dir_cache_do_very_best_to_load_dir(xs->cache, key);
  
  if (dir == NULL)
    return;
  else
    {
      dir_delete(dir);
    }
}

static void          
set_schema      (GConfSource* source,
                 const gchar* key,
                 const gchar* schema_key)
{
  XMLSource* xs = (XMLSource*)source;

  Dir* dir;
  gchar* parent;
  
  g_return_if_fail(schema_key != NULL);

  parent = gconf_key_directory(key);
  
  g_assert(parent != NULL);
  
  dir = dir_cache_create_or_load_dir(xs->cache, parent);
  
  g_free(parent);
  parent = NULL;

  if (dir == NULL)
    return; /* error should be set */
  else
    {
      const gchar* relative_key;
      
      relative_key = gconf_key_key(key);
      
      dir_set_schema(dir, relative_key, schema_key);
    }
}

static gboolean      
sync_all        (GConfSource* source)
{
  XMLSource* xs = (XMLSource*)source;

  return dir_cache_sync(xs->cache);
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
  gconf_log(GCL_INFO, _("Initializing XML backend module"));

  return NULL;
}

G_MODULE_EXPORT GConfBackendVTable* 
gconf_backend_get_vtable(void)
{
  return &xml_vtable;
}

/******************************************************/

/*
 *  XMLSource
 */ 

/* This timeout periodically cleans up
   the old cruft in the cache */
static gboolean
cleanup_timeout(gpointer data)
{
  XMLSource* xs = (XMLSource*)data;

  dir_cache_clean(xs->cache);

  return TRUE;
}

static XMLSource*
xs_new       (const gchar* root_dir)
{
  XMLSource* xs;

  g_return_val_if_fail(root_dir != NULL, NULL);

  xs = g_new0(XMLSource, 1);

  xs->root_dir = g_strdup(root_dir);

  xs->cache = dir_cache_new(xs, 300);

  xs->timeout_id = g_timeout_add(1000*60*5, /* 1 sec * 60 s/min * 5 min */
                                 cleanup_timeout,
                                 xs);
  
  return xs;
}

static void
xs_destroy   (XMLSource* xs)
{
  g_return_if_fail(xs != NULL);

  if (!g_source_remove(xs->timeout_id))
    {
      /* should not happen, don't translate */
      gconf_log(GCL_ERR, "timeout not found to remove?");
    }
  
  dir_cache_destroy(xs->cache);
  g_free(xs->root_dir);
  g_free(xs);
}

/*
 * DirCache
 */

typedef struct _SyncData SyncData;
typedef struct _CleanData CleanData;

static void dir_cache_sync_foreach(const gchar* key,
                                   Dir* dir,
                                   SyncData* sd);

static void dir_cache_destroy_foreach(const gchar* key,
                                      Dir* dir, gpointer data);

static void dir_cache_destroy_nonexistent_foreach(gchar* key,
                                                  gpointer val,
                                                  gpointer data);

static gboolean dir_cache_clean_foreach(const gchar* key,
                                        Dir* dir, CleanData* cd);

static DirCache*
dir_cache_new         (XMLSource* source, GTime length)
{
  DirCache* dc;

  dc = g_new(DirCache, 1);

  dc->source = source;
  
  dc->cache = g_hash_table_new(g_str_hash, g_str_equal);
  dc->nonexistent_cache = g_hash_table_new(g_str_hash, g_str_equal);

  dc->deleted = NULL;
  
  dc->length = length;

  return dc;
}

static void
dir_cache_destroy     (DirCache* dc)
{
  g_hash_table_foreach(dc->cache, (GHFunc)dir_cache_destroy_foreach,
                       NULL);
  g_hash_table_foreach(dc->nonexistent_cache,
                       (GHFunc)dir_cache_destroy_nonexistent_foreach,
                       NULL);
  g_hash_table_destroy(dc->cache);
  g_hash_table_destroy(dc->nonexistent_cache);
  g_free(dc);
}

static Dir*
dir_cache_lookup      (DirCache* dc,
                       const gchar* key)
{
  return g_hash_table_lookup(dc->cache, key);
}

static gboolean
dir_cache_lookup_nonexistent(DirCache* dc,
                             const gchar* key)
{
  return GPOINTER_TO_INT(g_hash_table_lookup(dc->nonexistent_cache,
                                             key));
}

static void
dir_cache_set_nonexistent   (DirCache* dc,
                             const gchar* key,
                             gboolean setting)
{
  if (setting)
    {
      /* don't use safe_ here, doesn't matter */
      g_hash_table_insert(dc->nonexistent_cache,
                          g_strdup(key),
                          GINT_TO_POINTER(TRUE));
    }
  else
    {
      gpointer origkey;
      gpointer origval;

      if (g_hash_table_lookup_extended(dc->nonexistent_cache,
                                       key,
                                       &origkey, &origval))
        {
          g_free(origkey);
          g_hash_table_remove(dc->nonexistent_cache,
                              key);
        }
    }
}

static void
dir_cache_insert      (DirCache* dc, Dir* d)
{
  safe_g_hash_table_insert(dc->cache, d->key, d);
}

static void
dir_cache_remove      (DirCache* dc, Dir* d)
{
  g_hash_table_remove(dc->cache, d->key);
  dir_destroy(d);
}

struct _SyncData {
  gboolean failed;
  DirCache* dc;
};

static gboolean
dir_cache_sync        (DirCache* dc)
{
  SyncData sd = { FALSE, dc };
  GSList* delete_list;

  /* First delete pending directories */
  delete_list = dc->deleted;

  while (delete_list != NULL)
    {
      GSList* tmp;

      tmp = delete_list->data;

      while (tmp != NULL)
        {
          Dir* d = tmp->data;

          if (!dir_sync(d))
            sd.failed = TRUE;
          
          tmp = g_slist_next(tmp);
        }

      g_slist_free(delete_list->data);
      
      delete_list = g_slist_next(delete_list);
    }

  g_slist_free(dc->deleted);
  dc->deleted = NULL;
  
  g_hash_table_foreach(dc->cache, (GHFunc)dir_cache_sync_foreach,
                       &sd);

  return !sd.failed;
}

struct _CleanData {
  GTime now;
  DirCache* dc;
};

static void
dir_cache_clean       (DirCache* dc)
{
  CleanData cd = { 0, dc };

  cd.now = time(NULL); /* ha ha, it's an online store! */

  g_hash_table_foreach_remove(dc->cache, (GHRFunc)dir_cache_clean_foreach,
                              &cd);
}

static void
dir_cache_delete_recursive(DirCache* dc, Dir* d, GSList** hit_list)
{  
  GSList* subdirs;
  GSList* tmp;
  
  subdirs = dir_all_subdirs(d);

  tmp = subdirs;
  while (tmp != NULL)
    {
      Dir* subd;

      subd = dir_cache_do_very_best_to_load_dir(dc, (gchar*)tmp->data);

      /* recurse, whee! (unless the subdir is already deleted) */
      if (subd != NULL &&
          !dir_deleted(subd))
        dir_cache_delete(dc, subd);

      tmp = g_slist_next(tmp);
    }

  /* The first directories to be deleted (fringes) go on the front
     of the list. */
  *hit_list = g_slist_prepend(*hit_list, d);
  /* We go ahead and mark the dir deleted */
  dir_delete(d);
}

static void
dir_cache_delete      (DirCache* dc, Dir* d)
{
  GSList* hit_list = NULL;

  dir_cache_delete_recursive(dc, d, &hit_list);

  /* If you first dir_cache_delete() a subdir, then dir_cache_delete()
     its parent, without syncing, first the list generated by
     the subdir delete then the list from the parent delete should
     be nuked. If you first delete a parent, then its subdir,
     really only the parent list should be nuked, but
     in effect it's OK to nuke the parent first then
     fail to nuke the subdir. So, if we prepend here,
     then nuke the list in order, it will work fine.
  */
  
  dc->deleted = g_slist_prepend(dc->deleted, hit_list);
}

static Dir*
dir_cache_do_very_best_to_load_dir(DirCache* dc,
                                   const gchar* key)
{
  Dir* dir;

  g_assert(key != NULL);

  /* Check cache */
  dir = dir_cache_lookup(dc, key);
  
  if (dir != NULL)
    {
      return dir;
    }
  else
    {
      /* Not in cache, check whether we already failed
         to load it */
      if (dir_cache_lookup_nonexistent(dc, key))
        {
          return NULL;
        }
      else
        {
          /* Didn't already fail to load, try to load */
          dir = dir_load(dc->source, key);
          
          if (dir != NULL)
            {
              /* Cache it */
              dir_cache_insert(dc, dir);
              
              return dir;
            }
          else
            {
              /* Remember that we failed to load it */
              dir_cache_set_nonexistent(dc, key, TRUE);
              
              return NULL;
            }
        }
    }
}

static Dir*
dir_cache_create_or_load_dir      (DirCache* dc,
                                   const gchar* key)
{
  Dir* dir;
  
  dir = dir_cache_do_very_best_to_load_dir(dc, key);
  
  if (dir == NULL)
    {
      gconf_clear_error(); /* Only pass an error up if we can't create */
      dir = dir_create(dc->source, key);

      if (dir == NULL)
        return NULL; /* error should be set */
      else
        dir_cache_insert(dc, dir);
    }

  return dir;
}

static void
dir_cache_sync_foreach(const gchar* key,
                       Dir* dir,
                       SyncData* sd)
{
  if (!dir_sync(dir))
    sd->failed = TRUE;
}

static void
dir_cache_destroy_foreach(const gchar* key,
                          Dir* dir, gpointer data)
{
  dir_destroy(dir);
}

static void
dir_cache_destroy_nonexistent_foreach(gchar* key,
                                      gpointer val,
                                      gpointer data)
{
  g_free(key);
}

static gboolean
dir_cache_clean_foreach(const gchar* key,
                        Dir* dir, CleanData* cd)
{
  GTime last_access = dir_last_access(dir);

  if ((cd->now - last_access) > cd->dc->length)
    {
      dir_destroy(dir);
      return TRUE;
    }
  else
    return FALSE;
}

/*
 * Dir
 */

/* entry */

typedef struct _Entry Entry;

struct _Entry {
  gchar* name; /* a relative key */
  gchar* schema_name;
  GConfValue* value;
  xmlNodePtr node;
  gchar* mod_user;
  GTime mod_time;
  guint dirty : 1;
};

Entry* entry_new     (void);
void   entry_destroy (Entry* e);
void   entry_sync    (Entry* e); /* syncs to the node */
void   entry_fill    (Entry* e, const gchar* name); /* syncs Entry from node */

/* xml manipulation */

static GConfValue* xentry_extract_value(xmlNodePtr node);
static void        xentry_set_value(xmlNodePtr node, GConfValue* value);

/* private dir func decls */

static void
dir_load_doc(Dir* d);

static Dir*
dir_new_blank(XMLSource* source, const gchar* key,
              gchar* fs_dirname, gchar* xml_filename);

static Entry* dir_make_new_entry(Dir* d, const gchar* relative_key);

static gboolean dir_forget_entry_if_useless(Dir* d, Entry* e);

/* dir implementations */
static Dir*
dir_load        (XMLSource* source, const gchar* key)
{
  Dir* d;
  gchar* fs_dirname;
  gchar* xml_filename;
  
  fs_dirname = gconf_concat_key_and_dir(source->root_dir, key);
  xml_filename = g_strconcat(fs_dirname, "/.gconf.xml", NULL);

  {
    struct stat s;
    gboolean error = FALSE;
    
    if (stat(xml_filename, &s) != 0)
      {
        gconf_set_error(GCONF_FAILED,
                         _("Could not stat `%s': %s"),
                         xml_filename, strerror(errno));
        error = TRUE;
      }
    else if (S_ISDIR(s.st_mode))
      {
        gconf_set_error(GCONF_FAILED,
                         _("XML filename `%s' is a directory"),
                         xml_filename);
        error = TRUE;
      }

    if (error)
      {
        g_free(fs_dirname);
        g_free(xml_filename);
        return NULL;
      }
  }
    
  d = dir_new_blank(source, key, fs_dirname, xml_filename);

  return d;
}

static gboolean
create_fs_dir(const gchar* dir, const gchar* xml_filename, const gchar* root_dir)
{
  if (gconf_file_test(dir, GCONF_FILE_ISDIR))
    return TRUE;

  /* Don't create anything above the root directory */
  if (strlen(dir) > strlen(root_dir))
    {
      gchar* parent;
      
      parent = parent_dir(dir);
      
      if (parent != NULL)
        {
          gchar* parent_xml = NULL;
          gboolean success = FALSE;
          
          if (xml_filename)
            parent_xml = g_strconcat(parent, "/.gconf.xml", NULL);
          
          success = create_fs_dir(parent, parent_xml, root_dir);

          g_free(parent);
          if (parent_xml)
            g_free(parent_xml);

          if (!success)
            return FALSE;
        }
    }
  
  if (mkdir(dir, 0700) < 0)
    {
      if (errno != EEXIST)
        {
          gconf_set_error(GCONF_FAILED,
                           _("Could not make directory `%s': %s"),
                           (gchar*)dir, strerror(errno));
          return FALSE;
        }
    }

  if (xml_filename != NULL)
    {
      int fd;
      /* don't truncate the file, it may well already exist */
      fd = open(xml_filename, O_CREAT | O_WRONLY, 0600);
      
      if (fd < 0)
        {
          gconf_set_error(GCONF_FAILED, _("Failed to create file `%s': %s"),
                           xml_filename, strerror(errno));
          
          return FALSE;
        }
      
      if (close(fd) < 0)
        {
          gconf_set_error(GCONF_FAILED, _("Failed to close file `%s': %s"),
                           xml_filename, strerror(errno));
          
          return FALSE;
        }
    }
  
  return TRUE;
}

static Dir*
dir_create      (XMLSource* source, const gchar* key)
{
  Dir* d;
  gchar* fs_dirname;
  gchar* xml_filename;
  
  fs_dirname = gconf_concat_key_and_dir(source->root_dir, key);
  xml_filename = g_strconcat(fs_dirname, "/.gconf.xml", NULL);

  if (!create_fs_dir(fs_dirname, xml_filename, source->root_dir))
    {
      /* Error is already set */
      g_free(fs_dirname);
      g_free(xml_filename);
      return NULL;
    }
  
  d = dir_new_blank(source, key, fs_dirname, xml_filename);
  
  return d;
}

static void
entry_destroy_foreach(const gchar* name, Entry* e, gpointer data)
{
  entry_destroy(e);
}

static void
dir_destroy     (Dir* d)
{
  g_free(d->key);
  g_free(d->fs_dirname);
  g_free(d->xml_filename);
  
  if (d->doc != NULL)
    xmlFreeDoc(d->doc);
  
  g_hash_table_foreach(d->entry_cache, (GHFunc)entry_destroy_foreach,
                       NULL);
  
  g_hash_table_destroy(d->entry_cache);
  
  g_free(d);
}

static void
entry_sync_foreach(const gchar* name, Entry* e, gpointer data)
{
  entry_sync(e);
}

static gboolean
dir_sync        (Dir* d)
{
  gboolean retval = TRUE;
  
  /* note that if we are deleted but already
     synced, this returns now, making the
     dircache's recursive delete tactic reasonably
     efficient
  */
  if (!d->dirty)
    return TRUE;  

  /* We should have a doc if dirty is TRUE */
  g_assert(d->doc != NULL);

  d->last_access = time(NULL);
  
  if (d->deleted)
    {
      if (unlink(d->xml_filename) != 0)
        {
          gconf_set_error(GCONF_FAILED, _("Failed to delete `%s': %s"),
                           d->xml_filename, strerror(errno));
          return FALSE;
        }

      if (rmdir(d->fs_dirname) != 0)
        {
          gconf_set_error(GCONF_FAILED, _("Failed to delete `%s': %s"),
                           d->fs_dirname, strerror(errno));
          return FALSE;
        }
    }
  else
    {
      gboolean old_existed = FALSE;
      gchar* tmp_filename;
      gchar* old_filename;
      
      /* First make sure entry values are synced to their
         XML nodes */
      g_hash_table_foreach(d->entry_cache, (GHFunc)entry_sync_foreach, NULL);
      
      tmp_filename = g_strconcat(d->fs_dirname, "/.gconf.xml.tmp", NULL);
      old_filename = g_strconcat(d->fs_dirname, "/.gconf.xml.old", NULL);

      if (xmlSaveFile(tmp_filename, d->doc) < 0)
        {
          gboolean recovered = FALSE;
          
          /* Try to solve the problem by creating the FS dir */
          if (!gconf_file_exists(d->fs_dirname))
            {
              if (create_fs_dir(d->fs_dirname, NULL, d->source->root_dir))
                {
                  if (xmlSaveFile(tmp_filename, d->doc) >= 0)
                    recovered = TRUE;
                }
            }

          if (!recovered)
            {
              /* I think libxml may mangle errno, but we might as well 
                 try. */
              gconf_set_error(GCONF_FAILED, _("Failed to write file `%s': %s"), 
                               tmp_filename, strerror(errno));
              
              retval = FALSE;
              
              goto failed_end_of_sync;
            }
        }

      old_existed = gconf_file_exists(d->xml_filename);

      if (old_existed)
        {
          if (rename(d->xml_filename, old_filename) < 0)
            {
              gconf_set_error(GCONF_FAILED, 
                               _("Failed to rename `%s' to `%s': %s"),
                               d->xml_filename, old_filename, strerror(errno));

              retval = FALSE;
              goto failed_end_of_sync;
            }
        }

      if (rename(tmp_filename, d->xml_filename) < 0)
        {
          gconf_set_error(GCONF_FAILED, _("Failed to rename `%s' to `%s': %s"),
                           tmp_filename, d->xml_filename, strerror(errno));

          /* Put the original file back, so this isn't a total disaster. */
          if (rename(old_filename, d->xml_filename) < 0)
            {
              gconf_set_error(GCONF_FAILED, _("Failed to restore `%s' from `%s': %s"),
                               d->xml_filename, old_filename, strerror(errno));
            }

          retval = FALSE;
          goto failed_end_of_sync;
        }

      if (old_existed)
        {
          if (unlink(old_filename) < 0)
            {
              gconf_log(GCL_WARNING, _("Failed to delete old file `%s': %s"),
                         old_filename, strerror(errno));
              /* Not a failure, just leaves cruft around. */
            }
        }

    failed_end_of_sync:
      
      g_free(old_filename);
      g_free(tmp_filename);
    }

  if (retval)
    d->dirty = FALSE;

  return retval;
}

static void
dir_set_value   (Dir* d, const gchar* relative_key, GConfValue* value)
{
  Entry* e;
  
  if (d->doc == NULL)
    dir_load_doc(d);
  
  e = g_hash_table_lookup(d->entry_cache, relative_key);
  
  if (e == NULL)
    e = dir_make_new_entry(d, relative_key);

  if (e->value)
    gconf_value_destroy(e->value);

  e->value = gconf_value_copy(value);

  d->last_access = time(NULL);
  e->mod_time = d->last_access;

  if (e->mod_user)
    g_free(e->mod_user);
  e->mod_user = g_strdup(g_get_user_name());
  
  e->dirty = TRUE;
  d->dirty = TRUE;
}

static GConfValue*
dir_get_value   (Dir* d, const gchar* relative_key, gchar** schema_name)
{
  Entry* e;

  if (d->doc == NULL)
    dir_load_doc(d);

  e = g_hash_table_lookup(d->entry_cache, relative_key);

  d->last_access = time(NULL);
  
  if (e == NULL || e->value == NULL)
    {
      if (schema_name && e && e->schema_name)
        *schema_name = g_strdup(e->schema_name);
      return NULL;
    }
  else
    {
      if (schema_name &&
          e->value->type == GCONF_VALUE_IGNORE_SUBSEQUENT &&
          e->schema_name)
        *schema_name = g_strdup(e->schema_name);
      
      return gconf_value_copy(e->value);
    }
}

static GConfMetaInfo*
dir_get_metainfo(Dir* d, const gchar* relative_key)
{
  GConfMetaInfo* gcmi;
  Entry* e;
  
  d->last_access = time(NULL);
  
  if (d->doc == NULL)
    dir_load_doc(d);

  e = g_hash_table_lookup(d->entry_cache, relative_key);

  if (e == NULL)
    return NULL;
  
  gcmi = gconf_meta_info_new();

  if (e->schema_name)
    gconf_meta_info_set_schema(gcmi, e->schema_name);

  if (e->mod_time != 0)
    gconf_meta_info_set_mod_time(gcmi, e->mod_time);

  if (e->mod_user)
    gconf_meta_info_set_mod_user(gcmi, e->mod_user);
  
  return gcmi;
}

static void
dir_unset_value (Dir* d, const gchar* relative_key)
{
  Entry* e;
  
  d->last_access = time(NULL);
  
  if (d->doc == NULL)
    dir_load_doc(d);

  e = g_hash_table_lookup(d->entry_cache, relative_key);
  
  if (e == NULL ||
      e->value == NULL) /* nothing to change */
    return;

  d->dirty = TRUE;

  g_assert(e->value != NULL);
  gconf_value_destroy(e->value);
  e->value = NULL;
  
  if (dir_forget_entry_if_useless(d, e))
    {
      /* entry is destroyed */
      return;
    }
  else
    {
      e->mod_time = d->last_access;
      
      if (e->mod_user)
        g_free(e->mod_user);
      e->mod_user = g_strdup(g_get_user_name());

      e->dirty = TRUE;
    }
}

typedef struct _ListifyData ListifyData;

struct _ListifyData {
  GSList* list;
  const gchar* name;
};

static void
listify_foreach(const gchar* key, Entry* e, ListifyData* ld)
{
  if (e->value)
    ld->list = g_slist_prepend(ld->list,
                               gconf_entry_new(g_strdup(key),
                                                gconf_value_copy(e->value)));
}

static GSList*
dir_all_entries (Dir* d)
{
  ListifyData ld;
  
  if (d->doc == NULL)
    dir_load_doc(d);

  ld.list = NULL;
  ld.name = d->key;

  g_hash_table_foreach(d->entry_cache, (GHFunc)listify_foreach,
                       &ld);
  
  return ld.list;
}

static GSList*
dir_all_subdirs (Dir* d)
{
  DIR* dp;
  struct dirent* dent;
  struct stat statbuf;
  GSList* retval = NULL;
  gchar* fullpath;
  gchar* fullpath_end;
  guint len;
  guint subdir_len;
  
  if (d->doc == NULL)
    dir_load_doc(d);

  dp = opendir(d->fs_dirname);

  if (dp == NULL)
    {
      gconf_set_error(GCONF_FAILED, _("Failed to open directory `%s': %s"),
                       d->fs_dirname, strerror(errno));
      return NULL;
    }

  len = strlen(d->fs_dirname);
  subdir_len = PATH_MAX - len;
  
  fullpath = g_malloc0(subdir_len + len + 20); /* ensure null termination */
  strcpy(fullpath, d->fs_dirname);
  
  fullpath_end = fullpath + len;
  *fullpath_end = '/';
  ++fullpath_end;
  *fullpath_end = '\0';

  while ((dent = readdir(dp)) != NULL)
    {
      /* ignore ., .., and all dot-files */
      if (dent->d_name[0] == '.')
        continue;

      len = strlen(dent->d_name);

      if (len < subdir_len)
        {
          strcpy(fullpath_end, dent->d_name);
          strncpy(fullpath_end+len, "/.gconf.xml", subdir_len - len);
        }
      else
        continue; /* Shouldn't ever happen since PATH_MAX is available */
      
      if (stat(fullpath, &statbuf) < 0)
        {
          /* This is some kind of cruft, not an XML directory */
          continue;
        }
      
      retval = g_slist_prepend(retval, g_strdup(dent->d_name));
    }

  /* if this fails, we really can't do a thing about it
     and it's not a meaningful error */
  closedir(dp);
  
  return retval;
}

static void
dir_set_schema  (Dir* d,
                 const gchar* relative_key,
                 const gchar* schema_key)
{
  Entry* e;

  if (d->doc == NULL)
    dir_load_doc(d);

  d->dirty = TRUE;
  d->last_access = time(NULL);
  
  e = g_hash_table_lookup(d->entry_cache, relative_key);

  if (e == NULL)
    e = dir_make_new_entry(d, relative_key);

  e->mod_time = d->last_access;
  
  if (e->schema_name)
    g_free(e->schema_name);

  if (schema_key != NULL)
    {
      e->schema_name = g_strdup(schema_key);
      e->dirty = TRUE;
    }
  else
    {
      e->schema_name = NULL;
      dir_forget_entry_if_useless(d, e);
    }
}

static void
dir_delete      (Dir* d)
{
  d->deleted = TRUE;
  d->dirty = TRUE;
  
  /* go ahead and free the XML document */

  if (d->doc)
    xmlFreeDoc(d->doc);
  d->doc = NULL;
}

static gboolean
dir_deleted     (Dir* d)
{
  return d->deleted;
}

static GTime
dir_last_access (Dir* d)
{
  return d->last_access;
}

/* private Dir functions */

static void
dir_fill_cache_from_doc(Dir* d);     

static void
dir_load_doc(Dir* d)
{
  gboolean xml_already_exists = TRUE;
  gboolean need_backup = FALSE;
  struct stat statbuf;
  
  g_return_if_fail(d->doc == NULL);

  if (stat(d->xml_filename, &statbuf) < 0)
    {
      switch (errno)
        {
        case ENOENT:
          xml_already_exists = FALSE;
          break;
        case ENOTDIR:
        case ELOOP:
        case EFAULT:
        case EACCES:
        case ENOMEM:
        case ENAMETOOLONG:
        default:
          /* These are all fatal errors */
          gconf_set_error(GCONF_FAILED, _("Failed to stat `%s': %s"),
                           d->xml_filename, strerror(errno));
          return;
          break;
        }
    }

  if (statbuf.st_size == 0)
    {
      xml_already_exists = FALSE;
    }

  if (xml_already_exists)
    d->doc = xmlParseFile(d->xml_filename);

  /* We recover from these errors instead of passing them up */

  /* This has the potential to just blow away an entire corrupted
     config file; but I think that is better than the alternatives
     (disabling config for a directory because the document is mangled)
  */  

  /* Also we create empty .gconf.xml files when we create a new dir,
     and those return a parse error */
  
  if (d->doc == NULL)
    {
      if (xml_already_exists)
        need_backup = TRUE; /* we want to save whatever broken stuff was in the file */
          
      /* Create a new doc */
      
      d->doc = xmlNewDoc("1.0");
    }
  
  if (d->doc->root == NULL)
    {
      /* fill it in */
      d->doc->root = xmlNewDocNode(d->doc, NULL, "gconf", NULL);
    }
  else if (strcmp(d->doc->root->name, "gconf") != 0)
    {
      xmlFreeDoc(d->doc);
      d->doc = xmlNewDoc("1.0");
      d->doc->root = xmlNewDocNode(d->doc, NULL, "gconf", NULL);
      need_backup = TRUE; /* save broken stuff */
    }
  else
    {
      /* We had an initial doc with a valid root */
      /* Fill child_cache from entries */
      dir_fill_cache_from_doc(d);
    }

  if (need_backup)
    {
      /* Back up the file we failed to parse, if it exists,
         we aren't going to be able to do anything if this call
         fails
      */
      
      gchar* backup = g_strconcat(d->xml_filename, ".bak", NULL);
      int fd;
      
      rename(d->xml_filename, backup);
      
      /* Recreate .gconf.xml to maintain our integrity and be sure
         all_subdirs works */
      /* If we failed to rename, we just give up and truncate the file */
      fd = open(d->xml_filename, O_CREAT | O_WRONLY | O_TRUNC, 0600);
      if (fd >= 0)
        close(fd);
      
      g_free(backup);
    }
  
  g_assert(d->doc != NULL);
  g_assert(d->doc->root != NULL);
}

static Dir*
dir_new_blank(XMLSource* source, const gchar* key, gchar* fs_dirname, gchar* xml_filename)
{
  Dir* d;
  
  d = g_new0(Dir, 1);

  d->source = source;
  d->key = g_strdup(key);

  d->xml_filename = xml_filename;
  d->fs_dirname = fs_dirname;
  
  d->last_access = time(NULL);
  d->doc = NULL;

  d->entry_cache = g_hash_table_new(g_str_hash, g_str_equal);
  
  d->dirty = FALSE;
  d->deleted = FALSE;

  return d;
}

static Entry*
dir_make_new_entry(Dir* d, const gchar* relative_key)
{
  Entry* e;

  e = entry_new();

  e->name = g_strdup(relative_key);
  e->node = xmlNewChild(d->doc->root, NULL, "entry", NULL);
  e->dirty = TRUE;
  e->mod_time = 0;
  e->mod_user = NULL;
  
  safe_g_hash_table_insert(d->entry_cache, e->name, e);

  return e;
}

static gboolean
dir_forget_entry_if_useless(Dir* d, Entry* e)
{
  if (e->schema_name ||
      e->value)
    return FALSE; /* not useless */

  if (e->node != NULL)
    {
      xmlUnlinkNode(e->node);
      xmlFreeNode(e->node);
      e->node = NULL;
    }

  g_hash_table_remove(d->entry_cache, e->name);

  entry_destroy(e);

  return TRUE;
}

static void
dir_fill_cache_from_doc(Dir* d)
{
  xmlNodePtr node;
  GSList* bad_entries = NULL;
  
  if (d->doc == NULL ||
      d->doc->root == NULL ||
      d->doc->root->childs == NULL)
    {
      /* Empty document - just return. */
      return;
    }

  node = d->doc->root->childs;

  while (node != NULL)
    {
      if (node->type == XML_ELEMENT_NODE && 
          (strcmp(node->name, "entry") == 0))
        {
          gchar* attr = xmlGetProp(node, "name");

          if (attr != NULL)
            {
              if (g_hash_table_lookup(d->entry_cache, attr) != NULL)
                {
                  gconf_log(GCL_WARNING,
                             _("Duplicate entry `%s' in `%s', deleting"),
                             attr, d->xml_filename);
                  bad_entries = g_slist_prepend(bad_entries, node);
                }
              else
                {
                  Entry* e;
                  e = entry_new();

                  e->node = node;
                  
                  entry_fill(e, attr);

                  safe_g_hash_table_insert(d->entry_cache, e->name, e);
                }

              free(attr);

            }
          else
            {
              gconf_log(GCL_WARNING,
                         _("Entry with no name in XML file `%s', deleting"),
                         d->xml_filename);
              bad_entries = g_slist_prepend(bad_entries, node);
            }
        }
      else
        {
          gconf_log(GCL_WARNING,
                     _("Toplevel node in XML file `%s' is not an <entry>, deleting"),
                     d->xml_filename);
          bad_entries = g_slist_prepend(bad_entries, node);
        }
      
      node = node->next;
    }

  if (bad_entries != NULL)
    {
      GSList* tmp;
      
      tmp = bad_entries;
      
      while (tmp != NULL)
        {
          node = tmp->data;
          
          xmlUnlinkNode(node);
          xmlFreeNode(node);
          
          tmp = g_slist_next(tmp);
        }

      g_slist_free(bad_entries);
      d->dirty = TRUE;
    }
}

/*
 * Entry
 */

Entry*
entry_new     (void)
{
  Entry* e;

  e = g_new0(Entry, 1);

  return e;
}

void
entry_destroy (Entry* e)
{
  if (e->name)
    g_free(e->name);

  if (e->value)
    gconf_value_destroy(e->value);

  if (e->mod_user)
    g_free(e->mod_user);
  
  g_free(e);
}

void
entry_sync    (Entry* e)
{
  g_return_if_fail(e->node != NULL);

  if (!e->dirty)
    return;

  /* Unset all properties, so we don't have old cruft. */
  if (e->node->properties)
    xmlFreePropList(e->node->properties);
  e->node->properties = NULL;
  
  xmlSetProp(e->node, "name", e->name);

  if (e->mod_time != 0)
    {
      gchar* str = g_strdup_printf("%u", (guint)e->mod_time);
      xmlSetProp(e->node, "mtime", str);
      g_free(str);
    }
  else
    xmlSetProp(e->node, "mtime", NULL); /* Unset */

  /* OK if schema_name is NULL, then we unset */
  xmlSetProp(e->node, "schema", e->schema_name);

  /* OK if mod_user is NULL, since it unsets */
  xmlSetProp(e->node, "muser", e->mod_user);
  
  xentry_set_value(e->node, e->value);

  e->dirty = FALSE;
}

void
entry_fill    (Entry* e, const gchar* name)
{
  gchar* tmp;

  g_return_if_fail(e->node != NULL);

  if (e->name != NULL)
    {
      g_free(e->name);
      e->name = NULL;
    } 
  
  if (name == NULL)
    {
      tmp = xmlGetProp(e->node, "name");
      
      if (tmp != NULL)
        {
          e->name = g_strdup(tmp);
          free(tmp);
        }
    }
  else
    e->name = g_strdup(name);
  
  tmp = xmlGetProp(e->node, "schema");
  
  if (tmp != NULL)
    {
      /* Filter any crap schemas that appear, some speed cost */
      if (gconf_valid_key(tmp, NULL))
        e->schema_name = g_strdup(tmp);
      else
        e->schema_name = NULL;

      free(tmp);
    }
      
  tmp = xmlGetProp(e->node, "mtime");

  if (tmp != NULL)
    {
      e->mod_time = gconf_string_to_gulong(tmp);
      free(tmp);
    }
  else
    e->mod_time = 0;

  tmp = xmlGetProp(e->node, "muser");

  if (tmp != NULL)
    {
      e->mod_user = g_strdup(tmp);
      free(tmp);
    }
  else
    e->mod_user = NULL;
  
  if (e->value != NULL)
    gconf_value_destroy(e->value);
  
  e->value = xentry_extract_value(e->node);
}

/*
 * XML manipulation
 */

static GConfValue*
schema_node_extract_value(xmlNodePtr node)
{
  GConfValue* value = NULL;
  gchar* sd_str;
  gchar* owner_str;
  gchar* stype_str;
  GConfSchema* sc;

  sd_str = xmlGetProp(node, "short_desc");
  owner_str = xmlGetProp(node, "owner");
  stype_str = xmlGetProp(node, "stype");

  sc = gconf_schema_new();

  if (sd_str)
    {
      gconf_schema_set_short_desc(sc, sd_str);
      free(sd_str);
    }
  if (owner_str)
    {
      gconf_schema_set_owner(sc, owner_str);
      free(owner_str);
    }
  if (stype_str)
    {
      GConfValueType stype;
      stype = gconf_value_type_from_string(stype_str);
      gconf_schema_set_type(sc, stype);
      free(stype_str);
    }

  if (node->childs != NULL)
    {
      GConfValue* default_value = NULL;
      gchar* ld_str = NULL;
      GSList* bad_nodes = NULL;
      xmlNodePtr iter = node->childs;

      while (iter != NULL)
        {
          if (iter->type == XML_ELEMENT_NODE)
            {
              if (default_value == NULL &&
                  strcmp(iter->name, "default") == 0)
                {
                  default_value = xentry_extract_value(iter);
                }
              else if (ld_str == NULL &&
                       strcmp(iter->name, "longdesc") == 0)
                {
                  ld_str = xmlNodeGetContent(iter);
                }
              else
                {
                  bad_nodes = g_slist_prepend(bad_nodes, iter);
                }
            }
          else
            bad_nodes = g_slist_prepend(bad_nodes, iter); /* what is this node? */

          iter = iter->next;
        }
      

      /* Remove the bad nodes from the parse tree */
      if (bad_nodes != NULL)
        {
          GSList* tmp = bad_nodes;
          
          while (tmp != NULL)
            {
              xmlUnlinkNode(tmp->data);
              xmlFreeNode(tmp->data);
              
              tmp = g_slist_next(tmp);
            }
          
          g_slist_free(bad_nodes);
        }

      if (default_value != NULL)
        gconf_schema_set_default_value_nocopy(sc, default_value);

      if (ld_str)
        {
          gconf_schema_set_long_desc(sc, ld_str);
          free(ld_str);
        }
    }
  
  value = gconf_value_new(GCONF_VALUE_SCHEMA);
      
  gconf_value_set_schema_nocopy(value, sc);

  return value;
}

/* this actually works on any node,
   not just <entry>, such as the <car>
   and <cdr> nodes and the <li> nodes and the
   <default> node
*/
static GConfValue*
xentry_extract_value(xmlNodePtr node)
{
  GConfValue* value = NULL;
  gchar* type_str;
  GConfValueType type = GCONF_VALUE_INVALID;

  type_str = xmlGetProp(node, "type");

  if (type_str == NULL)
    return NULL;
  
  type = gconf_value_type_from_string(type_str);

  free(type_str);
  
  switch (type)
    {
    case GCONF_VALUE_INVALID:
      {
        gconf_log(GCL_WARNING, _("A node has unknown \"type\" attribute `%s', ignoring"), type_str);
        return NULL;
      }
      break;
    case GCONF_VALUE_INT:
    case GCONF_VALUE_STRING:
    case GCONF_VALUE_BOOL:
    case GCONF_VALUE_FLOAT:
      {
        gchar* value_str;
        GConfError* err = NULL;
        
        value_str = xmlGetProp(node, "value");
        
        if (value_str == NULL)
          return NULL;

        value = gconf_value_new_from_string(type, value_str, &err);

        if (value == NULL)
          {
            gconf_set_error(err->num, err->str);
            gconf_error_destroy(err);
          }
        
        free(value_str);

        return value;
      }
      break;
    case GCONF_VALUE_IGNORE_SUBSEQUENT:
      {
        value = gconf_value_new(type);
        return value;
      }
      break;
    case GCONF_VALUE_SCHEMA:
      return schema_node_extract_value(node);
      break;
    case GCONF_VALUE_LIST:
      {
        xmlNodePtr iter;
        GSList* bad_nodes = NULL;
        GSList* values = NULL;
        GConfValueType list_type = GCONF_VALUE_INVALID;

        {
          gchar* s;
          s = xmlGetProp(node, "ltype");
          if (s != NULL)
            {
              list_type = gconf_value_type_from_string(s);
              free(s);
            }
        }

        switch (list_type)
          {
          case GCONF_VALUE_INVALID:
          case GCONF_VALUE_LIST:
          case GCONF_VALUE_PAIR:
            return NULL;
          default:
            break;
          }
        
        iter = node->childs;

        while (iter != NULL)
          {
            if (iter->type == XML_ELEMENT_NODE)
              {
                GConfValue* v = NULL;
                if (strcmp(iter->name, "li") == 0)
                  {
                    
                    v = xentry_extract_value(iter);
                    if (v == NULL)
                      bad_nodes = g_slist_prepend(bad_nodes, iter);
                    else if (v->type != list_type)
                      {
                        gconf_value_destroy(v);
                        v = NULL;
                        bad_nodes = g_slist_prepend(bad_nodes, iter);
                      }
                  }
                else
                  {
                    /* What the hell is this? */
                    bad_nodes = g_slist_prepend(bad_nodes, iter);
                  }

                if (v != NULL)
                  values = g_slist_prepend(values, v);
              }
            iter = iter->next;
          }
        
        /* Remove the bad nodes from the parse tree */
        if (bad_nodes != NULL)
          {
            GSList* tmp = bad_nodes;

            while (tmp != NULL)
              {
                xmlUnlinkNode(tmp->data);
                xmlFreeNode(tmp->data);

                tmp = g_slist_next(tmp);
              }

            g_slist_free(bad_nodes);
          }
        
        /* put them in order, set the value */
        values = g_slist_reverse(values);

        value = gconf_value_new(GCONF_VALUE_LIST);

        gconf_value_set_list_type(value, list_type);
        gconf_value_set_list_nocopy(value, values);

        return value;
      }
      break;
    case GCONF_VALUE_PAIR:
      {
        GConfValue* car = NULL;
        GConfValue* cdr = NULL;
        xmlNodePtr iter;
        GSList* bad_nodes = NULL;
        
        iter = node->childs;

        while (iter != NULL)
          {
            if (iter->type == XML_ELEMENT_NODE)
              {
                if (car == NULL && strcmp(iter->name, "car") == 0)
                  {
                    car = xentry_extract_value(iter);
                    if (car == NULL)
                      bad_nodes = g_slist_prepend(bad_nodes, iter);
                    else if (car->type == GCONF_VALUE_LIST ||
                             car->type == GCONF_VALUE_PAIR)
                      {
                        gconf_value_destroy(car);
                        car = NULL;
                        bad_nodes = g_slist_prepend(bad_nodes, iter);
                      }
                  }
                else if (cdr == NULL && strcmp(iter->name, "cdr") == 0)
                  {
                    cdr = xentry_extract_value(iter);
                    if (cdr == NULL)
                      bad_nodes = g_slist_prepend(bad_nodes, iter);
                    else if (cdr->type == GCONF_VALUE_LIST ||
                             cdr->type == GCONF_VALUE_PAIR)
                      {
                        gconf_value_destroy(cdr);
                        cdr = NULL;
                        bad_nodes = g_slist_prepend(bad_nodes, iter);
                      }
                  }
                else
                  {
                    /* What the hell is this? */
                    bad_nodes = g_slist_prepend(bad_nodes, iter);
                  }
              }
            iter = iter->next;
          }


        /* Remove the bad nodes */
        
        if (bad_nodes != NULL)
          {
            GSList* tmp = bad_nodes;

            while (tmp != NULL)
              {
                xmlUnlinkNode(tmp->data);
                xmlFreeNode(tmp->data);

                tmp = g_slist_next(tmp);
              }

            g_slist_free(bad_nodes);
          }

        /* Return the pair */
        value = gconf_value_new(GCONF_VALUE_PAIR);
        gconf_value_set_car_nocopy(value, car);
        gconf_value_set_cdr_nocopy(value, cdr);

        return value;
      }
      break;
    default:
      g_assert_not_reached();
      return NULL;
      break;
    }
}


static void
xentry_set_value(xmlNodePtr node, GConfValue* value)
{
  const gchar* type;
  gchar* value_str;

  g_return_if_fail(node != NULL);
  g_return_if_fail(value != NULL);

  type = gconf_value_type_to_string(value->type);
  
  xmlSetProp(node, "type", type);

  switch (value->type)
    {
    case GCONF_VALUE_IGNORE_SUBSEQUENT:
      break;

    case GCONF_VALUE_INT:
    case GCONF_VALUE_FLOAT:
    case GCONF_VALUE_BOOL:
    case GCONF_VALUE_STRING:
      value_str = gconf_value_to_string(value);
  
      xmlSetProp(node, "value", value_str);

      g_free(value_str);
      break;
    case GCONF_VALUE_SCHEMA:
      {
        GConfSchema* sc = gconf_value_schema(value);
        
        xmlSetProp(node, "value", NULL);
        xmlSetProp(node, "stype", gconf_value_type_to_string(sc->type));
        /* OK if these are set to NULL, since that unsets the property */
        xmlSetProp(node, "short_desc", sc->short_desc);
        xmlSetProp(node, "owner", sc->owner);

        if (node->childs)
          xmlFreeNodeList(node->childs);
        node->childs = NULL;
        node->last = NULL;

        if (sc->default_value != NULL)
          {
            xmlNodePtr default_value_node;
            default_value_node = xmlNewChild(node, NULL, "default", NULL);
            xentry_set_value(default_value_node, sc->default_value);
          }

        if (sc->long_desc)
          {
            xmlNodePtr ld_node;

            ld_node = xmlNewChild(node, NULL, "longdesc", sc->long_desc);
          }
      }
      break;
    case GCONF_VALUE_LIST:
      {
        GSList* list;

        /* Nuke any existing nodes */
        if (node->childs)
          xmlFreeNodeList(node->childs);
        node->childs = NULL;
        node->last = NULL;

        xmlSetProp(node, "ltype",
                   gconf_value_type_to_string(gconf_value_list_type(value)));
        
        /* Add a new child for each node */
        list = gconf_value_list(value);

        while (list != NULL)
          {
            xmlNodePtr child;
            /* this is O(1) because libxml saves the list tail */
            child = xmlNewChild(node, NULL, "li", NULL);

            xentry_set_value(child, (GConfValue*)list->data);
            
            list = g_slist_next(list);
          }
      }
      break;
    case GCONF_VALUE_PAIR:
      {
        xmlNodePtr car, cdr;

        if (node->childs)
          xmlFreeNodeList(node->childs);
        node->childs = NULL;
        node->last = NULL;

        car = xmlNewChild(node, NULL, "car", NULL);
        cdr = xmlNewChild(node, NULL, "cdr", NULL);

        xentry_set_value(car, gconf_value_car(value));
        xentry_set_value(cdr, gconf_value_cdr(value));
      }
      break;
    default:
      g_assert_not_reached();
      break;
    }
}


/*
 * Misc
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
