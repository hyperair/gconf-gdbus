/* GConf
 * Copyright (C) 1999, 2000, 2002 Red Hat Inc.
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
#include <gconf/gconf.h>

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

/*
 * Overview
 * 
 * Basically we have a directory tree underneath an arbitrary root
 * directory.  The directory tree reflects the configuration
 * namespace. Each directory contains an XML-like file which contains
 * metadata for the directory and the key-value pairs in that
 * directory.  The magic file in each directory is called %gconf.xml,
 * and can't clash with the database namespace because names containing
 * % aren't allowed.  So:
 *
 * /
 *  %gconf.xml
 *   guppi/
 *     %gconf.xml
 *   gnumeric/
 *     %gconf.xml
 *
 */

typedef struct
{
  GConfSource source; /* inherit from GConfSource */
  char* root_dir;
  guint timeout_id;
  GConfLock* lock;
  guint dir_mode;
  guint file_mode;
} MarkupSource;

static MarkupSource* ms_new     (const char   *root_dir,
                                 guint         dir_mode,
                                 guint         file_mode,
                                 GConfLock    *lock);
static void          ms_destroy (MarkupSource *source);

/*
 * VTable functions
 */

 /* shutdown() is a BSD libc function */
static void           x_shutdown      (GError           **err);
static GConfSource*   resolve_address (const char        *address,
                                       GError           **err);
static void           lock            (GConfSource       *source,
                                       GError           **err);
static void           unlock          (GConfSource       *source,
                                       GError           **err);
static gboolean       readable        (GConfSource       *source,
                                       const char        *key,
                                       GError           **err);
static gboolean       writable        (GConfSource       *source,
                                       const char        *key,
                                       GError           **err);
static GConfValue*    query_value     (GConfSource       *source,
                                       const char        *key,
                                       const char       **locales,
                                       char             **schema_name,
                                       GError           **err);
static GConfMetaInfo* query_metainfo  (GConfSource       *source,
                                       const char        *key,
                                       GError           **err);
static void           set_value       (GConfSource       *source,
                                       const char        *key,
                                       const GConfValue  *value,
                                       GError           **err);
static GSList*        all_entries     (GConfSource       *source,
                                       const char        *dir,
                                       const char       **locales,
                                       GError           **err);
static GSList*        all_subdirs     (GConfSource       *source,
                                       const char        *dir,
                                       GError           **err);
static void           unset_value     (GConfSource       *source,
                                       const char        *key,
                                       const char        *locale,
                                       GError           **err);
static gboolean       dir_exists      (GConfSource       *source,
                                       const char        *dir,
                                       GError           **err);
static void           remove_dir      (GConfSource       *source,
                                       const char        *dir,
                                       GError           **err);
static void           set_schema      (GConfSource       *source,
                                       const char        *key,
                                       const char        *schema_key,
                                       GError           **err);
static gboolean       sync_all        (GConfSource       *source,
                                       GError           **err);
static void           destroy_source  (GConfSource       *source);
static void           clear_cache     (GConfSource       *source);



static GConfBackendVTable markup_vtable = {
  x_shutdown,
  resolve_address,
  lock,
  unlock,
  readable,
  writable,
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
  destroy_source,
  clear_cache
};

static void          
x_shutdown (GError** err)
{
  gconf_log (GCL_DEBUG, _("Unloading text markup backend module."));
}

static void
lock (GConfSource* source,
      GError** err)
{
  

}

static void
unlock (GConfSource* source,
        GError** err)
{


}

static gboolean
readable (GConfSource *source,
          const char  *key,
          GError     **err)
{

  return TRUE;
}

static gboolean
writable (GConfSource  *source,
          const char   *key,
          GError      **err)
{

  return TRUE;
}

static GConfSource*  
resolve_address (const char *address,
                 GError    **err)
{
  char* root_dir;
  MarkupSource* xsource;
  GConfSource* source;
  guint len;
  gint flags = 0;
  GConfLock* lock = NULL;
  guint dir_mode = 0700;
  guint file_mode = 0600;
  char** address_flags;
  char** iter;
  gboolean force_readonly;
  
  root_dir = gconf_address_resource(address);

  if (root_dir == NULL)
    {
      gconf_set_error(err, GCONF_ERROR_BAD_ADDRESS, _("Couldn't find the XML root directory in the address `%s'"), address);
      return NULL;
    }

  /* Chop trailing '/' to canonicalize */
  len = strlen(root_dir);

  if (root_dir[len-1] == '/')
    root_dir[len-1] = '\0';

  if (mkdir(root_dir, dir_mode) < 0)
    {
      if (errno != EEXIST)
        {
          gconf_set_error(err, GCONF_ERROR_FAILED,
                          _("Could not make directory `%s': %s"),
                          (char*)root_dir, strerror(errno));
          g_free(root_dir);
          return NULL;
        }
      else
        {
          /* Already exists, base our dir_mode on it */
          struct stat statbuf;
          if (stat(root_dir, &statbuf) == 0)
            {
              dir_mode = _gconf_mode_t_to_mode (statbuf.st_mode);
              /* dir_mode without search bits */
              file_mode = dir_mode & (~0111); 
            }
        }
    }

  force_readonly = FALSE;
  
  address_flags = gconf_address_flags (address);  
  if (address_flags)
    {
      iter = address_flags;
      while (*iter)
        {
          if (strcmp (*iter, "readonly") == 0)
            {
              force_readonly = TRUE;
              break;
            }

          ++iter;
        }
    }

  g_strfreev (address_flags);

  {
    /* See if we're writable */
    gboolean writable;
    int fd;
    char* testfile;

    writable = FALSE;
    
    if (!force_readonly)
      {
        testfile = g_strconcat(root_dir, "/.testing.writeability", NULL);    
        
        fd = open(testfile, O_CREAT|O_WRONLY, S_IRWXU);
        
        if (fd >= 0)
          {
            writable = TRUE;
            close(fd);
          }
        
        unlink(testfile);
        
        g_free(testfile);
      }
    
    if (writable)
      flags |= GCONF_SOURCE_ALL_WRITEABLE;

    /* We only do locking if it's writable,
       which is sort of broken but close enough
    */
    if (writable)
      {
        char* lockdir;

        lockdir = gconf_concat_dir_and_key(root_dir, "%gconf-xml-backend.lock");
        
        lock = gconf_get_lock(lockdir, err);

        if (lock != NULL)
          gconf_log(GCL_DEBUG, "Acquired lock directory `%s'", lockdir);
        
        g_free(lockdir);
        
        if (lock == NULL)
          {
            g_free(root_dir);
            return NULL;
          }
      }
  }

  {
    /* see if we're readable */
    gboolean readable = FALSE;
    DIR* d;

    d = opendir(root_dir);

    if (d != NULL)
      {
        readable = TRUE;
        closedir(d);
      }
    
    if (readable)
      flags |= GCONF_SOURCE_ALL_READABLE;
  }

  if (!(flags & GCONF_SOURCE_ALL_READABLE) &&
      !(flags & GCONF_SOURCE_ALL_WRITEABLE))
    {
      gconf_set_error(err, GCONF_ERROR_BAD_ADDRESS, _("Can't read from or write to the XML root directory in the address `%s'"), address);
      g_free(root_dir);
      return NULL;
    }  
  
  /* Create the new source */

  xsource = ms_new(root_dir, dir_mode, file_mode, lock);

  gconf_log(GCL_DEBUG,
            _("Directory/file permissions for XML source at root %s are: %o/%o"),
            root_dir, dir_mode, file_mode);
  
  source = (GConfSource*)xsource;

  source->flags = flags;
  
  g_free(root_dir);
  
  return source;
}

static GConfValue* 
query_value (GConfSource *source,
             const char  *key,
             const char **locales,
             char       **schema_name,
             GError     **err)
{
  MarkupSource* xs = (MarkupSource*)source;
  char* parent;
  Dir* dir;
  GError* error = NULL;

  parent = gconf_key_directory(key);
  
  g_assert(parent != NULL);
  
  dir = cache_lookup(xs->cache, parent, FALSE, &error);

  /* We DO NOT want to return an error unless it represents a general
     problem with the backend; since we don't want to report stuff
     like "this key doesn't exist yet" - however this is a maintenance
     problem, since some errors may be added that need reporting. */
  if (error != NULL)
    {
      gconf_log(GCL_WARNING, "%s", error->message);
      g_error_free(error);
      error = NULL;
    }
  
  g_free(parent);
  parent = NULL;
  
  if (dir != NULL)
    {
      const char* relative_key;
      GConfValue* retval;
      
      relative_key = gconf_key_key(key);

      retval = dir_get_value(dir, relative_key, locales, schema_name, &error);

      /* perhaps we should be reporting this error... */
      if (error != NULL)
        {
          gconf_log(GCL_WARNING, "%s", error->message);
          g_error_free(error);
          error = NULL;
        }
      
      return retval;
    }
  else
    return NULL;
}

static GConfMetaInfo*
query_metainfo (GConfSource *source,
                const char  *key,
                GError     **err)
{
  MarkupSource* xs = (MarkupSource*)source;
  char* parent;
  Dir* dir;

  parent = gconf_key_directory(key);

  if (parent != NULL)
    {
      dir = cache_lookup(xs->cache, parent, FALSE, err);
      g_free(parent);
      parent = NULL;
      
      if (dir != NULL)
        {
          const char* relative_key;
      
          relative_key = gconf_key_key (key);

          return dir_get_metainfo (dir, relative_key, err);
        }
    }

  /* No metainfo found */
  return NULL;
}

static void          
set_value (GConfSource* source, const char* key, const GConfValue* value,
           GError** err)
{
  MarkupSource* xs = (MarkupSource*)source;
  Dir* dir;
  char* parent;
  
  g_return_if_fail(value != NULL);
  g_return_if_fail(source != NULL);
  
  parent = gconf_key_directory(key);
  
  g_assert(parent != NULL);
  
  dir = cache_lookup(xs->cache, parent, TRUE, err);
  
  g_free(parent);
  parent = NULL;

  if (dir == NULL)
    {
      g_return_if_fail((err == NULL || *err != NULL));
      return;
    }
  else
    {
      const char* relative_key;
      
      relative_key = gconf_key_key(key);
      
      dir_set_value(dir, relative_key, value, err);
    }
}


static GSList*             
all_entries    (GConfSource* source,
                const char* key,
                const char** locales,
                GError** err)
{
  MarkupSource* xs = (MarkupSource*)source;
  Dir* dir;

  dir = cache_lookup(xs->cache, key, FALSE, err);
  
  if (dir == NULL)
    return NULL;
  else
    return dir_all_entries(dir, locales, err);
}

static GSList*
all_subdirs     (GConfSource* source,
                 const char* key,
                 GError** err)
{
  Dir* dir;
  MarkupSource* xs = (MarkupSource*)source;
  GError *sync_err;
  
  /* We have to sync before we can do this, to see which
   * subdirs have gone away.
   */
  sync_err = NULL;
  cache_sync (xs->cache, &sync_err);
  if (sync_err)
    {
      gconf_log (GCL_WARNING, _("Error syncing the XML backend directory cache: %s"),
                 sync_err->message);
      g_error_free (sync_err);
      sync_err = NULL;
      /* continue, may as well try our best. */
    }
  
  dir = cache_lookup (xs->cache, key, FALSE, err);
  
  if (dir == NULL)
    return NULL;
  else
    return dir_all_subdirs (dir, err);
}

static void          
unset_value     (GConfSource* source,
                 const char* key,
                 const char* locale,
                 GError** err)
{
  MarkupSource* xs = (MarkupSource*)source;
  Dir* dir;
  char* parent;

  gconf_log(GCL_DEBUG, "XML backend: unset value `%s'", key);
  
  parent = gconf_key_directory(key);
  
  dir = cache_lookup(xs->cache, parent, FALSE, err);

  g_free(parent);
  
  if (dir == NULL)
    return;
  else
    {
      const char* relative_key;
  
      relative_key = gconf_key_key(key);

      dir_unset_value(dir, relative_key, locale, err);
    }
}

static gboolean
dir_exists      (GConfSource*source,
                 const char* key,
                 GError** err)
{
  MarkupSource *xs = (MarkupSource*)source;
  Dir* dir;
  
  dir = cache_lookup(xs->cache, key, FALSE, err);
  
  return (dir != NULL);
}  

static void          
remove_dir      (GConfSource* source,
                 const char* key,
                 GError** err)
{
  g_set_error (err, GCONF_ERROR,
               GCONF_ERROR_FAILED,
               _("Remove dir operation is no longer supported, just remove all the values in the directory"));
}

static void          
set_schema (GConfSource *source,
            const char *key,
            const char *schema_key,
            GError     **err)
{
  MarkupSource* xs = (MarkupSource*)source;

  Dir* dir;
  char* parent;

  g_return_if_fail (source != NULL);
  g_return_if_fail (key != NULL);

  parent = gconf_key_directory (key);
  
  g_assert (parent != NULL);
  
  dir = cache_lookup (xs->cache, parent, TRUE, err);
  
  g_free (parent);
  parent = NULL;

  if (dir == NULL)
    return; /* error should be set */
  else
    {
      const char* relative_key;
      
      relative_key = gconf_key_key (key);
      
      dir_set_schema (dir, relative_key, schema_key, err);
    }
}

static gboolean      
sync_all        (GConfSource* source,
                 GError** err)
{
  MarkupSource* xs = (MarkupSource*)source;

  return cache_sync (xs->cache, err);
}

static void          
destroy_source  (GConfSource* source)
{
  ms_destroy((MarkupSource*)source);
}

static void
clear_cache     (GConfSource* source)
{
  MarkupSource* xs = (MarkupSource*)source;

  /* clean all entries older than 0 seconds */
  cache_clean(xs->cache, 0);
}

/* Initializer */

G_MODULE_EXPORT const char*
g_module_check_init (GModule *module)
{
  gconf_log (GCL_DEBUG, _("Initializing Markup backend module"));

  return NULL;
}

G_MODULE_EXPORT GConfBackendVTable* 
gconf_backend_get_vtable (void)
{
  return &markup_vtable;
}

/* ****************************************************/

/*
 *  MarkupSource
 */ 

/* This timeout periodically cleans up
   the old cruft in the cache */
static gboolean
cleanup_timeout(gpointer data)
{
  MarkupSource* xs = (MarkupSource*)data;

  cache_clean(xs->cache, 60*5 /* 5 minutes */);

  return TRUE;
}

static MarkupSource*
ms_new       (const char* root_dir, guint dir_mode, guint file_mode, GConfLock* lock)
{
  MarkupSource* xs;

  g_return_val_if_fail(root_dir != NULL, NULL);

  xs = g_new0(MarkupSource, 1);

  xs->root_dir = g_strdup(root_dir);

  xs->cache = cache_new(xs->root_dir, dir_mode, file_mode);

  xs->timeout_id = g_timeout_add(1000*60*5, /* 1 sec * 60 s/min * 5 min */
                                 cleanup_timeout,
                                 xs);

  xs->lock = lock;

  xs->dir_mode = dir_mode;
  xs->file_mode = file_mode;
  
  return xs;
}

static void
ms_destroy   (MarkupSource* xs)
{
  GError* error = NULL;
  
  g_return_if_fail(xs != NULL);

  /* do this first in case we're in a "fast cleanup just before exit"
     situation */
  if (xs->lock != NULL && !gconf_release_lock(xs->lock, &error))
    {
      gconf_log(GCL_ERR, _("Failed to give up lock on XML dir `%s': %s"),
                xs->root_dir, error->message);
      g_error_free(error);
      error = NULL;
    }
  
  if (!g_source_remove(xs->timeout_id))
    {
      /* should not happen, don't translate */
      gconf_log(GCL_ERR, "timeout not found to remove?");
    }
  
  cache_destroy(xs->cache);
  g_free(xs->root_dir);
  g_free(xs);
}

