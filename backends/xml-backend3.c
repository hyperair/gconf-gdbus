
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

typedef struct _XMLSource XMLSource;

/** Dir **/

typedef struct _Dir Dir;

struct _Dir {
  XMLSource* source;
  gchar* key;
  gchar* fs_dirname;
  gchar* xml_filename;
  GTime last_access; /* so we know when to un-cache */
  GTime mod_time;    /* time of last dir modification */
  xmlDocPtr doc;
  GHashTable* child_cache; /* store subdirs and key-value entries */
  guint dirty : 1;
  guint deleted : 1;
};

/* returns NULL if the load fails. */
static Dir*        dir_load        (XMLSource* source, const gchar* key);
static Dir*        dir_create      (XMLSource* source, const gchar* key);
static void        dir_destroy     (Dir* d);
static gboolean    dir_sync        (Dir* d);
 /* key should have no slashes in it */
static void        dir_set_value   (Dir* d, const gchar* relative_key);
static GConfValue* dir_get_value   (Dir* d, const gchar* relative_key);
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
static void        dir_cache_sync        (DirCache* dc);
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
};

static XMLSource* xs_new       (const gchar* root_dir);
static void       xs_destroy   (XMLSource* source);

/*
 * VTable functions
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
  XMLSource* xs = (XMLSource*)source;
  gchar* parent;
  Dir* dir;

  parent = g_conf_key_directory(key);
  
  g_assert(parent != NULL);
  
  dir = xs_do_very_best_to_load_dir(xs, parent);

  g_free(parent);
  parent = NULL;
  
  if (dir != NULL)
    {
      const gchar* relative_key;
  
      relative_key = g_conf_key_key(key);

      return dir_get_value(dir, relative_key);
    }
  else
    return NULL;
}

static GConfMetaInfo*
query_metainfo  (GConfSource* source, const gchar* key_or_dir)
{
  XMLSource* xs = (XMLSource*)source;

  return NULL; /* for now */
}

static void          
set_value (GConfSource* source, const gchar* key, GConfValue* value)
{
  XMLSource* xs = (XMLSource*)source;
  Dir* dir;
  gchar* parent;
  
  g_return_if_fail(value != NULL);

  parent = g_conf_key_directory(key);
  
  g_assert(parent != NULL);
  
  dir = xs_create_or_load_dir(xs, parent);
  
  g_free(parent);
  parent = NULL;

  if (dir == NULL)
    return; /* error should be set */
  else
    {
      const gchar* relative_key;
      
      relative_key = g_conf_key_key(key);
      
      dir_set_value(dir, relative_key, value);
    }
}


static GSList*             
all_entries    (GConfSource* source,
                const gchar* key)
{
  XMLSource* xs = (XMLSource*)source;
  Dir* dir;

  dir = xs_do_very_best_to_load_dir(xs, key);
  
  if (dir == NULL)
    return NULL;
  else
    return dir_all_entries(dir);
}

static GSList*
all_subdirs     (GConfSource* source,
                 const gchar* key)

{  
  XMLSource* xs = (XMLSource*)source;

  dir = xs_do_very_best_to_load_dir(xs, key);
  
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
  
  dir = xs_do_very_best_to_load_dir(xs, key);
  
  if (dir == NULL)
    return;
  else
    {
      const gchar* relative_key;
  
      relative_key = g_conf_key_key(key);

      dir_unset_value(dir, relative_key);
    }
}

static void          
remove_dir      (GConfSource* source,
                 const gchar* key)
{
  XMLSource* xs = (XMLSource*)source;

  dir = xs_do_very_best_to_load_dir(xs, key);
  
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
  
  g_return_if_fail(value != NULL);

  parent = g_conf_key_directory(key);
  
  g_assert(parent != NULL);
  
  dir = xs_create_or_load_dir(xs, parent);
  
  g_free(parent);
  parent = NULL;

  if (dir == NULL)
    return; /* error should be set */
  else
    {
      const gchar* relative_key;
      
      relative_key = g_conf_key_key(key);
      
      dir_set_schema(dir, relative_key, schema_key);
    }
}

static gboolean      
sync_all        (GConfSource* source)
{
  XMLSource* xs = (XMLSource*)source;

  dir = xs_do_very_best_to_load_dir(xs, key);
  
  if (dir == NULL)
    return;
  else
    {
      dir_sync(dir);
    }
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

/******************************************************/

/*
 *  XMLSource
 */ 

static XMLSource*
xs_new       (const gchar* root_dir)
{
  XMLSource* xs;

  g_return_val_if_fail(root_dir != NULL, NULL);

  xs = g_new0(XMLSource, 1);

  xs->root_dir = g_strdup(root_dir);

  xs->cache = dir_cache_new(xs);

  return xs;
}

static void
xs_destroy   (XMLSource* xs)
{
  g_return_if_fail(xs != NULL);

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

static void
dir_cache_sync        (DirCache* dc)
{
  SyncData sd = { FALSE, dc };
  
  g_hash_table_foreach(dc->cache, (GHFunc)dir_cache_sync_foreach,
                       &sd);
  
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
  *hit_list = g_slist_prepend(hit_list, d);
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
  
  dc->deleted = g_slist_prepend(dc->delete, hit_list);
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
          dir = dir_load(xs, key);
          
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
  
  dir = dir_cache_do_very_best_to_load_dir(xs, key);
  
  if (dir == NULL)
    {
      dir = dir_create(dc->source, parent);

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
                        Dir* dir, gpointer data)
{
  GTime last_access = dir_last_access(dir);

  if ((cd.now - last_access) > cd->dc->length)
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

static void
dir_load_doc(Dir* d);

static Dir*
dir_new_blank(XMLSource* source, const gchar* key,
              gchar* xml_filename, gchar* fs_dirname);

static Dir*
dir_load        (XMLSource* source, const gchar* key)
{
  xmlDocPtr 
  Dir* d;
  gchar* fs_dirname;
  gchar* xml_filename;
  
  fs_dirname = g_conf_concat_key_and_dir(source->root_dir, key);
  xml_filename = g_strconcat(fs_dirname, "/.gconf.xml", NULL);

  {
    struct stat s;
    gboolean error = FALSE;
    
    if (stat(xml_filename, &s) != 0)
      {
        g_conf_set_error(G_CONF_FAILED,
                         _("Could not stat `%s': %s"),
                         xml_filename, strerror(errno));
        error = TRUE;
      }
    else if (S_ISDIR(s.st_mode))
      {
        g_conf_set_error(G_CONF_FAILED,
                         _("XML filename `%s' is a directory"),
                         xml_filename);
        error = TRUE;
      }

    if (error)
      {
        g_free(fs_filename);
        g_free(xml_filename);
        return NULL;
      }
  }
    
  d = dir_new_blank(source, key, fs_dirname, xml_filename);

  return d;
}

static Dir*
dir_create      (XMLSource* source, const gchar* key)
{


}

static void
dir_destroy     (Dir* d)
{
  g_free(d->key);
  g_free(d->fs_dirname);
  g_free(d->xml_filename);
  
  if (d->doc != NULL)
    xmlFreeDoc(d->doc);
  
  /* FIXME foreach destroy the child entries */
  g_hash_table_destroy(d->child_cache);

  g_free(d);
}

static gboolean
dir_sync        (Dir* d)
{


}

static void
dir_set_value   (Dir* d, const gchar* relative_key)
{


}

static GConfValue*
dir_get_value   (Dir* d, const gchar* relative_key)
{


}

static GConfMetaInfo*
dir_get_metainfo(Dir* d, const gchar* relative_key)
{


}

static void
dir_unset_value (Dir* d, const gchar* relative_key)
{


}

static GSList*
dir_all_entries (Dir* d)
{


}

static GSList*
dir_all_subdirs (Dir* d)
{

}

static void
dir_set_schema  (Dir* d,
                 const gchar* relative_key,
                 const gchar* schema_key)
{


}
static void
dir_delete      (Dir* d)
{
  d->deleted = TRUE;

  /* FIXME go ahead and free the XML document, stuff like that */
  
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
dir_load_doc(Dir* d)
{
  xmlDocPtr doc;
  
  doc = xmlParseFile(d->xml_filename);

  if (doc == NULL)
    {
      g_conf_set_error(G_CONF_FAILED,
                       _("Couldn't load file `%s'"), d->xml_filename);
      return;
    }

  if (doc->root == NULL ||
      strcmp(doc->root->name, "gconf") != 0)
    {
      g_conf_set_error(G_CONF_FAILED,
                       _("Empty or wrong type document `%s'"),
                       d->xml_filename);
      return;
    }

  d->doc = doc;
  
  
  /* FIXME find mod time in doc */

  /* FIXME fill child_cache from entries and subdirs */


}


static Dir*
dir_new_blank(XMLSource* source, const gchar* key, gchar* xml_filename)
{
  Dir* d;
  
  d = g_new0(Dir, 1);

  d->source = source;
  d->key = g_strdup(key);

  d->xml_filename = xml_filename;
  
  d->last_access = time(NULL);
  d->mod_time = 0;
  d->doc = NULL;

  d->child_cache = g_hash_table_new(g_str_hash, g_str_equal);

  d->dirty = FALSE;
  d->deleted = FALSE;

  return d;
}
