
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
static void        dir_unset_value (Dir* d, const gchar* relative_key);
static GSList*     dir_all_entries (Dir* d);
static GSList*     dir_all_subdirs (Dir* d);
/* doesn't have to be empty */
static gboolean    dir_delete      (Dir* d);
static GTime       dir_last_access (Dir* d);

/** DirCache **/

typedef struct _DirCache DirCache;

struct _DirCache {
  XMLSource* source;
  GHashTable* cache;
  GTime length; /* amount of time to keep cached items */
};

static DirCache*   dir_cache_new         (XMLSource* source, GTime length);
static void        dir_cache_destroy     (DirCache* dc);
static Dir*        dir_cache_lookup      (DirCache* dc,
                                          const gchar* key);
static void        dir_cache_insert      (DirCache* dc, Dir* d);
static void        dir_cache_remove      (DirCache* dc, Dir* d);
static void        dir_cache_sync        (DirCache* dc);
static void        dir_cache_clean       (DirCache* dc);

/** XMLSource **/

struct _XMLSource {
  GConfSource source;
  gchar* root_dir;
  DirCache* cache;
};

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

static gboolean dir_cache_clean_foreach(const gchar* key,
                                        Dir* dir, CleanData* cd);

static DirCache*
dir_cache_new         (XMLSource* source, GTime length)
{
  DirCache* dc;

  dc = g_new(DirCache, 1);

  dc->source = source;
  
  dc->cache = g_hash_table_new(g_str_hash, g_str_equal);

  dc->length = length;
}

static void
dir_cache_destroy     (DirCache* dc)
{
  g_hash_table_foreach(dc->cache, (GHFunc)dir_cache_destroy_foreach,
                       NULL);
  g_hash_table_destroy(dc->cache);
  g_free(dc);
}

static Dir*
dir_cache_lookup      (DirCache* dc,
                       const gchar* key)
{
  return g_hash_table_lookup(dc->cache, key);
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



