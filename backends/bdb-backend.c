/* 
 * GConf BerkeleyDB back-end
 *
 * Copyright (C) 2000 Sun Microsystems Inc
 * Contributed to the GConf project.
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/param.h>
#include <gconf/gconf-backend.h>
#include <gconf/gconf-internals.h>
#include <gconf/gconf.h>

#include "bdb.h"
#include "dir-utils.h"

#include <db.h>

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <limits.h>

typedef struct _BDBSource BDBSource;

struct _BDBSource
{
  GConfSource source;		/* inherit from GConfSource */
  BDB_Store *bdb;
  gchar *root_dir;
  guint timeout_id;
  GConfLock *lock;
};

/* *** { Forward declaration of all vtable functions */

static void vtable_bdb_shutdown (GError ** err);

static GConfSource *vtable_bdb_resolve_address (const gchar * address,
						GError ** err);

static void vtable_bdb_lock (GConfSource * source, GError ** err);

static void vtable_bdb_unlock (GConfSource * source, GError ** err);

static gboolean vtable_bdb_readable (GConfSource * source,
				     const gchar * key, GError ** err);

static gboolean vtable_bdb_writeable (GConfSource * source,
				      const gchar * key, GError ** err);

static GConfValue *vtable_bdb_query_value (GConfSource * source,
					   const gchar * key,
					   const gchar ** locales,
					   gchar ** schema_name,
					   GError ** err);

static GConfMetaInfo *vtable_bdb_query_metainfo (GConfSource * source,
						 const gchar * key,
						 GError ** err);

static void vtable_bdb_set_value (GConfSource * source,
				  const gchar * key,
				  const GConfValue * value, GError ** err);

static GSList *vtable_bdb_all_entries (GConfSource * source,
				       const gchar * dir,
				       const gchar ** locales,
				       GError ** err);

static GSList *vtable_bdb_all_subdirs (GConfSource * source,
				       const gchar * dir, GError ** err);

static void vtable_bdb_unset_value (GConfSource * source,
				    const gchar * key,
				    const gchar * locale, GError ** err);

static gboolean vtable_bdb_dir_exists (GConfSource * source,
				       const gchar * dir, GError ** err);

static void vtable_bdb_remove_dir (GConfSource * source,
				   const gchar * dir, GError ** err);

static void vtable_bdb_set_schema (GConfSource * source,
				   const gchar * key,
				   const gchar * schema_key,
				   GError ** err);

static gboolean vtable_bdb_sync_all (GConfSource * source, GError ** err);

static void vtable_bdb_destroy_source (GConfSource * source);

static void vtable_bdb_clear_cache (GConfSource * source);

/* *** } Forward declaration of all vtable functions */

static GConfBackendVTable _bdb_vtable = {
  vtable_bdb_shutdown,
  vtable_bdb_resolve_address,
  vtable_bdb_lock,
  vtable_bdb_unlock,
  vtable_bdb_readable,
  vtable_bdb_writeable,
  vtable_bdb_query_value,
  vtable_bdb_query_metainfo,
  vtable_bdb_set_value,
  vtable_bdb_all_entries,
  vtable_bdb_all_subdirs,
  vtable_bdb_unset_value,
  vtable_bdb_dir_exists,
  vtable_bdb_remove_dir,
  vtable_bdb_set_schema,
  vtable_bdb_sync_all,
  vtable_bdb_destroy_source,
  vtable_bdb_clear_cache
};

/* *** { localised key utility functions */

const char LOCALISED_MARKER[] = "%locale%";
static char localised_buf[MAXPATHLEN];

gboolean bdb_is_localised (const gchar * key)
{
  char *sflag_char = strchr (key, '%');

  if (!sflag_char)
    return FALSE;
  if (strlen (sflag_char) > strlen (LOCALISED_MARKER))
    {
      return memcmp (sflag_char, LOCALISED_MARKER,
		     sizeof (LOCALISED_MARKER) - 1) == 0;
    }
  return FALSE;
}

static char *
get_localised_key (const gchar * key, const gchar * locale)
{
  size_t len = strlen (key) + strlen (LOCALISED_MARKER) + strlen (locale) + 2;
  char *lkey = localised_buf;
  char *s;

  if (len <= sizeof (localised_buf))
    lkey = localised_buf;
  else
    lkey = malloc (len);

  strcpy (lkey, key);
  s = strrchr (lkey, '/');
  sprintf (s, "%s%s/%s", LOCALISED_MARKER, locale, gconf_key_key (key));
  return lkey;
}

static char *
get_localised_dir (const gchar * dir, const gchar * locale)
{
  size_t len = strlen (dir) + strlen (LOCALISED_MARKER) + strlen (locale) + 1;
  char *ldir = localised_buf;
  char *s;

  if (len <= sizeof (localised_buf))
    ldir = localised_buf;
  else
    ldir = malloc (len);

  sprintf (ldir, "%s/%s%s", dir, LOCALISED_MARKER, locale);
  return ldir;
}

static void
free_localised_buf (char *key)
{
  if (key == localised_buf)
    return;
  free (key);
}

gboolean bdb_is_default_locale (const gchar * locale)
{
  /* no locale - should be equivalent to C locale */
  return ((*locale == '\0') || (strcmp (locale, "C") == 0));
}

/* *** } localised key utility functions */

/* *** { BerkeleyDB backend methods */

static void
vtable_bdb_shutdown (GError ** err)
{
  gconf_log (GCL_INFO, _("Unloading BerkeleyDB (BDB) backend module."));
}

/* vtable_bdb_resolve_address:
 *
 * Parameters:
 * address = store type and location of GConf database
 * err (returned if non-NULL) = error associated with key or schema-retrieval (if any)
 *
 * Purpose:
 */
static GConfSource *
vtable_bdb_resolve_address (const gchar * address, GError ** err)
{
  BDBSource *xsource = NULL;
  GConfSource *source;

  xsource = (BDBSource *) calloc (1, sizeof (BDBSource));
  source = (GConfSource *) xsource;
  xsource->root_dir =
    _gconf_get_root_dir (address, &source->flags, "BerkeleyDB", err);
  xsource->bdb = bdb_new (xsource->root_dir, DB_CREATE);

  gconf_log (GCL_INFO, _("Opened BerkeleyDB source at root %s"),
	     xsource->root_dir);

  return source;
}

static void
vtable_bdb_lock (GConfSource * source, GError ** err)
{
  /* TODO */
}

static void
vtable_bdb_unlock (GConfSource * source, GError ** err)
{
  /* TODO */
}

static gboolean
vtable_bdb_readable (GConfSource * source,
		     const gchar * key, GError ** err)
{
  /* TODO: implement key-specific read/write control */
  g_assert (source);
  return source->flags | GCONF_SOURCE_ALL_READABLE;
}

static gboolean
vtable_bdb_writeable (GConfSource * source,
		      const gchar * key, GError ** err)
{
  /* TODO: implement key-specific read/write control */
  g_assert (source);
  return source->flags | GCONF_SOURCE_ALL_WRITEABLE;
}

/* vtable_bdb_query_value:
 *
 * Parameters:
 * source = store to read
 * key = full-path key
 * locales = NULL-terminated list of preferred locales, preferred first
 * schema_name (returned if non-NULL) = schema key associated with 'key'
 * err (returned if non-NULL) = error associated with key or schema-retrieval (if any)
 *
 * Purpose:
 * Returns value associated with 'key', or the schema-information if no value
 * is stored. Uses 'locales' to identify preferred values.
 */
static GConfValue *
vtable_bdb_query_value (GConfSource * source,
			const gchar * key,
			const gchar ** locales,
			gchar ** schema_name, GError ** err)
{
  BDBSource *xs = (BDBSource *) source;
  GConfValue *value;
  const gchar *locale;

  if (!locales || (*locales == 0))
    {
      return bdb_query_value (xs->bdb, key, schema_name, err);
    }
  while (locale = *locales++)
    {
      if (bdb_is_default_locale (locale))
	{
	  value = bdb_query_value (xs->bdb, key, schema_name, err);
	}
      else
	{
	  char *localised_key = get_localised_key (key, locale);
	  value = bdb_query_value (xs->bdb, localised_key, schema_name, err);
	  free_localised_buf (localised_key);
	}
      if (value != NULL)
	return value;
    }
  return NULL;
}

/* vtable_bdb_query_metainfo:
 * 
 * Parameters:
 * source = store to read
 * key = full-path key
 * err (returned if non-NULL) = error associated with schema-metainfo
 * retrieval (if any)
 *
 * Purpose:
 * Returns a GConfMetaInfo (containing schema-name, and last-modification info
 * for the schema setting) for a given key.
 */
static GConfMetaInfo *
vtable_bdb_query_metainfo (GConfSource * source,
			   const gchar * key, GError ** err)
{
  BDBSource *xs = (BDBSource *) source;
  gchar *parent;

  parent = gconf_key_directory (key);

  if (parent != NULL)
    {
      /* TODO: return meta-info, if any */
    }

  /* No metainfo found */
  return NULL;
}

/* vtable_bdb_set_value:
 *
 * Parameters:
 * source = store to write
 * key = full-path key
 * err (returned if non-NULL) = error associated with key or schema-retrieval (if any)
 *
 * Purpose:
 * Returns a GConfMetaInfo (containing schema-name, and last-modification info
 * for the schema setting) for a given key.
 */
static void
vtable_bdb_set_value (GConfSource * source, const gchar * key,
		      const GConfValue * value, GError ** err)
{
  BDBSource *xs = (BDBSource *) source;

  g_return_if_fail (value != NULL);
  g_return_if_fail (source != NULL);

  bdb_put_value (xs->bdb, key, value, err);
}


/* vtable_bdb_all_entries:
 *
 * Parameters:
 * source = store to read
 * key = full-path key
 * locales = NULL-terminated list of preferred locales, preferred first
 * err (returned if non-NULL) = error associated with key or schema-retrieval (if any)
 *
 * Purpose:
 * Returns all entries in the key directory identified by the given 'key'.
 */
static GSList *
vtable_bdb_all_entries (GConfSource * source,
			const gchar * dir,
			const gchar ** locales, GError ** err)
{
  BDBSource *xs = (BDBSource *) source;
  GSList *list = NULL;
  const gchar *locale;

  if (!locales || (*locales == 0))
    {
      /* didn't specify any locales */
      return bdb_all_entries (xs->bdb, dir, NULL, err);
    }
  while (locale = *locales++)
    {
      char *localised_dir;

      if (bdb_is_default_locale (locale))
	{
	  list = bdb_all_entries (xs->bdb, dir, list, err);
	}
      else
	{
	  localised_dir = get_localised_dir (dir, locale);
	  list = bdb_all_entries (xs->bdb, localised_dir, list, err);
	  free_localised_buf (localised_dir);
	}
    }
  return list;
}

/* vtable_bdb_all_subdirs:
 *
 * Parameters:
 * source = store to read
 * key = full-path key
 * err (returned if non-NULL) = error associated with key or schema-retrieval (if any)
 *
 * Purpose:
 * Returns a GSList containing all keys identifying sub-directories of
 * the given 'key'.
 */
static GSList *
vtable_bdb_all_subdirs (GConfSource * source,
			const gchar * key, GError ** err)
{
  BDBSource *xs = (BDBSource *) source;

  return bdb_all_subdirs (xs->bdb, key, err);
}

/* vtable_bdb_unset_value:
 * 
 * Parameters:
 * source = store to read
 * key = full-path key
 * locale = identifies locale value to be unset
 * err (returned if non-NULL) = error associated with key or schema-retrieval (if any)
 *
 * Purpose:
 * Returns a GSList containing all keys identifying sub-directories of
 * the given 'key'.
 */
static void
vtable_bdb_unset_value (GConfSource * source,
			const gchar * key,
			const gchar * locale, GError ** err)
{
  BDBSource *xs = (BDBSource *) source;

  gconf_log (GCL_DEBUG, "BDB backend: unset value `%s'", key);

  bdb_unset_value (xs->bdb, key, locale, err);
}

/* vtable_bdb_dir_exists:
 *
 * Parameters:
 * source = store to read
 * key = full-path key
 * err (returned if non-NULL) = error associated with key or schema-retrieval (if any)
 *
 * Purpose:
 * Returns a GSList containing all keys identifying sub-directories of
 * the given 'key'.
 */
static gboolean
vtable_bdb_dir_exists (GConfSource * source,
		       const gchar * key, GError ** err)
{
  BDBSource *xs = (BDBSource *) source;

  return bdb_dir_exists (xs->bdb, key, err);
}

/* vtable_bdb_remove_dir:
 *
 * Parameters:
 * source = store to read
 * key = full-path key
 * err (returned if non-NULL) = error associated with key or schema-retrieval (if any)
 *
 * Purpose:
 * Returns a GSList containing all keys identifying sub-directories of
 * the given 'key'.
 */
static void
vtable_bdb_remove_dir (GConfSource * source,
		       const gchar * key, GError ** err)
{
  BDBSource *xs = (BDBSource *) source;

  g_assert (xs != 0);
  g_assert (key != 0);

  bdb_remove_dir (xs->bdb, key, err);
}

/* vtable_bdb_set_schema:
 *
 * Parameters:
 * source = store to read
 * key = full-path key
 * schema_key = full-path key
 * err (returned if non-NULL) = error associated with key or schema-retrieval (if any)
 *
 * Purpose:
 * Returns a GSList containing all keys identifying sub-directories of
 */
static void
vtable_bdb_set_schema (GConfSource * source,
		       const gchar * key,
		       const gchar * schema_key, GError ** err)
{
  BDBSource *xs = (BDBSource *) source;

  bdb_set_schema (xs->bdb, key, schema_key, err);
}

/* vtable_bdb_sync_all:
 *
 * Parameters:
 * source = store to write
 * err (returned if non-NULL)
 *
 * Purpose:
 * Flushes cached data to the database.
 */
static gboolean
vtable_bdb_sync_all (GConfSource * source, GError ** err)
{
  BDBSource *xs = (BDBSource *) source;

  /* TODO */
  return TRUE;
}

/* vtable_bdb_destroy_source:
 *
 * Parameters:
 * source = store to write
 *
 * Purpose:
 * Destroys a GConfSource (like C++ object destructor).
 */
static void
vtable_bdb_destroy_source (GConfSource * source)
{
  BDBSource *xsource = (BDBSource *) source;
  g_assert (source);
  g_assert (xsource->bdb);
  bdb_close (xsource->bdb);
  free (xsource->root_dir);
  xsource->root_dir = 0;
  xsource->bdb = 0;
}

static void
vtable_bdb_clear_cache (GConfSource * source)
{
  BDBSource *xs = (BDBSource *) source;

  /* clean all entries older than 0 seconds */
  /* TODO */
}

/* *** } BerkeleyDB backend methods */

/* Initializer */

G_MODULE_EXPORT const gchar *
g_module_check_init (GModule * module)
{
  gconf_log (GCL_INFO, _("Initializing BDB backend module"));

  return NULL;
}

G_MODULE_EXPORT GConfBackendVTable *
gconf_backend_get_vtable (void)
{
  return &_bdb_vtable;
}
