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

#include <sys/types.h>
#include <sys/param.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gconf/gconf.h>
#include <gconf/gconf-internals.h>

#include "config.h"
#include "bdb.h"
#include "dir-utils.h"
#include "val-encode.h"

#define CLEAR_STRUCT(x) (memset(&x, 0, sizeof(x)))

enum eDirectoryId
{
  INVALID_DIR_ID = (guint32) - 1
};

/* #define GCONF_ENABLE_BDB_DEBUG 1 */
#define LOCKING 1

static const char *sysname = "GConf(bdb)";

#ifdef LOCKING
static DB_ENV *bdb_db_env;
#else
#define bdb_db_env 0
#endif

extern GConfValue *bdb_get_value (BDB_Store * bdb, const char *key);
extern GConfValue *bdb_restore_value (const char *srz);
static guint32
add_dir_to_parent (BDB_Store * bdb, guint32 parent_id, const char *dir);
static char *get_schema_key (BDB_Store * bdb, const char *key);

static void
show_error (DB * dbp, int ret, const char *type, const char *instance)
{
  char buf[256];

  sprintf (buf, "%s(%s)", type, instance);
  dbp->err (dbp, ret, buf);
}

static void
exit_error (DB * dbp, int ret, const char *type, const char *instance)
{
  show_error (dbp, ret, type, instance);
  exit (1);
}

static void
bdb_init (const char *dir)
{
  static int inited = 0;
#ifdef LOCKING
  int ret;
#endif

  if (inited)
    return;
  else
    inited = 1;
#ifdef LOCKING
  if (db_env_create (&bdb_db_env, 0) != 0)
    {
      bdb_db_env = 0;
      return;
    }
  /*
   * TODO: mode value should provide appropriate permissions; a
   * user-private database should not be accessible to group/other,
   * while a shared db should be available to all (DESIGN: currently no
   * way to pass this through GConf backend interface).
   */
  if ((ret =
       bdb_db_env->open (bdb_db_env, dir,
			 DB_CREATE | DB_INIT_LOCK | DB_INIT_MPOOL |
			 DB_INIT_TXN, 0)) != 0)
    {
      bdb_db_env->remove (bdb_db_env, dir, 0);
      bdb_db_env = 0;
    }
#endif
}


int
bdb_open_table (DB ** dbpp, const char *dbname, int flags, gboolean is_data)
{
  DB *dbp;
  int ret;

#ifdef LOCKING
  if (!bdb_db_env)
    {
      fprintf (stderr, "%s: dbenv not create, can't create %s (%s)\n",
	       sysname, dbname, db_strerror (ret));
      return (-1);
    }
#endif
  /* Create and initialize database object, open the database. */
  if ((ret = db_create (dbpp, bdb_db_env, 0)) != 0)
    {
      fprintf (stderr, "%s: db_create: %s\n", sysname, db_strerror (ret));
      return (-1);
    }
  dbp = *dbpp;
  dbp->set_errfile (dbp, stderr);
  dbp->set_errpfx (dbp, sysname);
  if ((ret = dbp->set_pagesize (dbp, 1024)) != 0)
    {
      dbp->err (dbp, ret, "set_pagesize");
      goto err1;
    }
  /*
   * if ((ret = dbp->set_cachesize (dbp, 0, 32 * 1024, 0)) != 0) {
   * dbp->err (dbp, ret, "set_cachesize"); goto err1; }
   */
  if (!is_data)
    {
      /*
       * non-data tables may have duplicate keys, so long as
       * key/value pair is unique
       */
      dbp->set_flags (dbp, DB_DUP | DB_DUPSORT);
#ifdef GCONF_ENABLE_BDB_DEBUG
      fprintf (stderr, "%s: Database %s permits duplicate keys\n", sysname,
	       dbname);
#endif
    }
  if ((ret = dbp->open (dbp, dbname, NULL, DB_BTREE, flags, 0664)) != 0)
    {
      dbp->err (dbp, ret, "%s: open", dbname);
      goto err1;
    }
  return 0;

err1:
  (void) dbp->close (dbp, 0);
  return -1;
}

int
bdb_open_dir_table (DB ** dbpp, const char *dir, const char *dbname,
		    int flags, gboolean is_data)
{
  if (!dir)
    {
      return bdb_open_table (dbpp, dbname, flags, is_data);
    }
  else
    {
      char *path;
      int ret;

      path = (char *) malloc (strlen (dir) + strlen (dbname) + 2);
      sprintf (path, "%s/%s", dir, dbname);
      ret = bdb_open_table (dbpp, path, flags, is_data);
      free (path);
      return ret;
    }
}

int
bdb_open (BDB_Store * bdb, const char *dir, int flags)
{
  if (!dir)
    return 0;
  memset (bdb, 0, sizeof (*bdb));

  bdb_init (dir);
#ifdef LOCKING
  dir = NULL;	/* ignore the directory, it has been specified for the db group */
#endif

  /* { open or create the databases */
  if (bdb_open_dir_table (&bdb->dbdirp, dir, DBD_DIR, flags, TRUE) != 0)
    {
    cleanup:
      bdb_close (bdb);
      return 1;
    }
  if (bdb_open_dir_table (&bdb->dbkeyp, dir, DBD_KEY, flags, FALSE) != 0)
    goto cleanup;
  if (bdb_open_dir_table
      (&bdb->dbhierp, dir, DBD_HIERARCHY, flags, FALSE) != 0)
    goto cleanup;
  if (bdb_open_dir_table (&bdb->dbvalp, dir, DBD_VALUE, flags, TRUE) != 0)
    goto cleanup;
  if (bdb_open_dir_table (&bdb->dbschp, dir, DBD_SCHEMA, flags, TRUE) != 0)
    goto cleanup;
  if (bdb_open_dir_table (&bdb->dbschkeyp, dir, DBD_SCHKEY, flags, TRUE) != 0)
    goto cleanup;
  /* } */

  /* { create cursors for searching with the KEY and SCHKEY databases */
  if (bdb->dbschkeyp->cursor (bdb->dbschkeyp, NULL, &bdb->schkeycp, 0) != 0)
    goto cleanup;
  if (bdb->dbkeyp->cursor (bdb->dbkeyp, NULL, &bdb->keycp, 0) != 0)
    goto cleanup;
  /* } */

  if (flags | DB_CREATE)
    {
      add_dir_to_parent (bdb, INVALID_DIR_ID, "/");
    }
  return 0;
}

BDB_Store *
bdb_new (const char *dir, int flags)
{
  BDB_Store *bdb = (BDB_Store *) calloc (1, sizeof (BDB_Store));

  if (bdb_open (bdb, dir, flags) != 0)
    {
      bdb_close (bdb);
      free (bdb);
      return 0;
    }
  return bdb;
}

int
bdb_create (BDB_Store * bdb, const char *dir)
{
  return bdb_open (bdb, dir, DB_CREATE | DB_TRUNCATE);
}

static void
close_cursor_or_error (DB * dbp, DBC * dbcp, const char *dbname)
{
  int ret;

  if ((ret = dbcp->c_close (dbcp)) != 0)
    {
      show_error (dbp, ret, "DB->cursor", dbname);
    }
}

void
bdb_close (BDB_Store * bdb)
{
  if (bdb->schkeycp)
    close_cursor_or_error (bdb->dbschkeyp, bdb->schkeycp, DBD_KEY);
  if (bdb->keycp)
    close_cursor_or_error (bdb->dbkeyp, bdb->keycp, DBD_KEY);
  if (bdb->dbdirp)
    bdb->dbdirp->close (bdb->dbdirp, 0);
  if (bdb->dbkeyp)
    bdb->dbkeyp->close (bdb->dbkeyp, 0);
  if (bdb->dbhierp)
    bdb->dbhierp->close (bdb->dbhierp, 0);
  if (bdb->dbvalp)
    bdb->dbvalp->close (bdb->dbvalp, 0);
  if (bdb->dbschp)
    bdb->dbschp->close (bdb->dbvalp, 0);
  if (bdb->dbschkeyp)
    bdb->dbschkeyp->close (bdb->dbschkeyp, 0);

  memset (bdb, 0, sizeof (*bdb));	/* trap errors sooner */
  free (bdb);
}

/* { Functions to init a passed DBT or return an initialised static DBT */

void
init_dbt_string (DBT * keyp, const char *key)
{
  keyp->data = (void *) key;
  keyp->size = strlen (key) + 1;
}

void
init_dbt_int (DBT * keyp, const guint32 * key)
{
  keyp->data = (void *) key;
  keyp->size = sizeof (*key);
}

DBT *
temp_key_string (const char *key)
{
  static DBT dbt;

  dbt.data = (void *) key;
  dbt.size = strlen (key) + 1;

  return &dbt;
}

DBT *
temp_key_int (int akey)
{
  static DBT dbt;
  static int key;

  key = akey;

  dbt.data = &key;
  dbt.size = sizeof (key);

  return &dbt;
}

/* } */

static guint32
get_dir_id (BDB_Store * bdb, const char *dir)
{
  int ret;
  DBT dirid;
  guint32 n;

  if (strcmp (dir, "/") == 0)
    {
      return 0;
    }
  CLEAR_STRUCT (dirid);
  if (
      (ret =
       bdb->dbdirp->get (bdb->dbdirp, NULL, temp_key_string (dir), &dirid,
			 0)) != 0)
    {
#ifdef GCONF_ENABLE_BDB_DEBUG
      fprintf (stderr, "%s: get_dir_id(\"%s\") -> not found!\n", sysname,
	       dir);
#endif
      return INVALID_DIR_ID;
    }
  memcpy (&n, dirid.data, sizeof (n));
#ifdef GCONF_ENABLE_BDB_DEBUG
  fprintf (stderr, "%s: get_dir_id(\"%s\") -> %d\n", sysname, dir, n);
#endif
  return ntohl (n);
}

static const char ROOT[] = "/";

static char *
parent_of (const char *keypath)
{
  char *dir;
  char *lastSlash;
  size_t len;

  lastSlash = strrchr (keypath, '/');
  if (!lastSlash)
    {
      return (char *) ROOT;
    }
  len = lastSlash - keypath;
  if (len == 0)
    {
      return (char *) ROOT;
    }
  dir = (char *) malloc (len + 1);
  memcpy (dir, keypath, len);
  dir[len] = '\0';
  return dir;
}

static const char *
safe_gconf_key_key (const char *keyp)
{
  const char *key = strrchr (keyp, '/');
  if (!key)
    return keyp;
  else
    return key + 1;
}

static void
free_dir (char *dir)
{
  if (dir != ROOT)
    free (dir);
}

/* { Functions to create directories and maintain the dir hierarchy */

static guint32
get_or_create_dir (BDB_Store * bdb, const char *dir)
{
  guint32 parent_id;
  guint32 dir_id;
  char *parent;

  dir_id = get_dir_id (bdb, dir);
  if (dir_id != INVALID_DIR_ID)
    return dir_id;

  parent = parent_of (dir);
  parent_id = get_or_create_dir (bdb, parent);
  free_dir (parent);
  if (parent_id == INVALID_DIR_ID)
    return INVALID_DIR_ID;

  return add_dir_to_parent (bdb, parent_id, dir);
}

static guint32
get_lock_id ()
{
  static int inited = 0;
  static guint32 id = 0;

  if (!inited)
    {
      lock_id (bdb_db_env, &id);
      inited = 1;
    }
  return id;
}

static guint32
add_dir_to_parent (BDB_Store * bdb, guint32 parent_id, const char *dirp)
{
  guint32 dir_id;
  int idir;
  DBT dir;
  DBT kdir_id;
  int ret;
  DB_LOCK lock;

  CLEAR_STRUCT (dir);
  CLEAR_STRUCT (kdir_id);
  init_dbt_string (&dir, "");
  init_dbt_int (&kdir_id, &dir_id);

#if LOCKING
  if (lock_get
      (bdb_db_env, get_lock_id (), 0, &dir, DB_LOCK_WRITE, &lock) != 0)
    {
      /* TODO: error failed to lock id row of DB_DIR table */
#ifdef GCONF_ERROR_VERBOSE
      fprintf (stderr, "%s: error failed to lock id row of DB_DIR table\n",
	       sysname);
#endif
      return INVALID_DIR_ID;
    }
#endif

  ret = bdb->dbdirp->get (bdb->dbdirp, NULL, &dir, &kdir_id, 0);
  if (ret != 0)
    {
      /*
       * No directory count record in database - initialising for
       * first time
       */
      dir_id = idir = 0;
      ret = bdb->dbdirp->put (bdb->dbdirp, NULL, &dir, &kdir_id, 0);
      if (ret != 0)
	{
#ifdef GCONF_ERROR_VERBOSE
	  fprintf (stderr,
		   "%s: error failed to update id row of DB_DIR table\n",
		   sysname);
#endif
	  ret = INVALID_DIR_ID;
	  goto unlock_and_return;
	}
    }
  else
    {
      /* Increment the directory count and put back to database */
      dir_id = *(guint32 *) kdir_id.data;
      dir_id = ntohl (dir_id);
      /* dir_id is currently in host format */
      dir_id = htonl (dir_id + 1);	/* return dir_id to net
					 * format */

      init_dbt_int (&kdir_id, &dir_id);
      ret = bdb->dbdirp->put (bdb->dbdirp, NULL, &dir, &kdir_id, 0);
      if (ret != 0)
	{
#ifdef GCONF_ERROR_VERBOSE
	  fprintf (stderr,
		   "%s: CRITICAL failed to put directory row in DB_DIR table\n",
		   sysname);
#endif
	  ret = INVALID_DIR_ID;
	  goto unlock_and_return;
	}
    }
  init_dbt_string (&dir, dirp);
  ret = bdb->dbdirp->put (bdb->dbdirp, NULL, &dir, &kdir_id, 0);
  if (ret != 0)
    {
#ifdef GCONF_ERROR_VERBOSE
      fprintf (stderr,
	       "%s: error failed to insert directory '%s' in DB_DIR table\n",
	       sysname, dir);
#endif
      ret = INVALID_DIR_ID;
      goto unlock_and_return;
    }
  ret = bdb->dbhierp->put (bdb->dbhierp, NULL, temp_key_int (parent_id),
			   temp_key_string (safe_gconf_key_key (dirp)), 0);
  if (ret != 0)
    {
#ifdef GCONF_ERROR_VERBOSE
      fprintf (stderr,
	       "%s: error failed to insert directory '%s' in DB_HIER table\n",
	       sysname, dir);
#endif
      ret = INVALID_DIR_ID;
      goto unlock_and_return;
    }
#ifdef GCONF_ENABLE_BDB_DEBUG
  fprintf (stderr, "%s: add_dir_to_parent(parent_id=%d, dir=%s) -> %d\n",
	   sysname, parent_id, dirp, dir_id);
#endif

  ret = dir_id;
unlock_and_return:
#ifdef LOCKING
  if (lock_put (bdb_db_env, &lock) != 0)
    {
#ifdef GCONF_ERROR_VERBOSE
      fprintf (stderr, "%s: CRITICAL failed to unlock BDB dir database\n",
	       sysname);
#endif
    }
#endif
  return ret;
}

/* } */

static void
put_key (BDB_Store * bdb, const char *keypath, const char *value, size_t len)
{
  DBT val;
  char *dir = parent_of (keypath);
  guint32 id = get_or_create_dir (bdb, dir);
  int ret;
  DBT *tkeyp;
  DBT *skeyp;

  free_dir (dir);
  CLEAR_STRUCT (val);
  val.data = (void *) value;
  val.size = len;

#ifdef GCONF_ENABLE_BDB_DEBUG
  fprintf (stderr, "%s: put_key(%s, %d, %s) with value '%s'\n", sysname,
	   keypath, id, safe_gconf_key_key (keypath), value);
#endif

  if (id == INVALID_DIR_ID)
    return;

  tkeyp = temp_key_string (keypath);
  ret =
    bdb->dbvalp->put (bdb->dbvalp, NULL, temp_key_string (keypath), &val, 0);
  if (ret != 0)
    {
#ifdef GCONF_ERROR_VERBOSE
      fprintf (stderr, "%s: failed to set value '%s' for key '%s'\n", sysname,
	       (char *) val.data, (char *) tkeyp->data);
#endif
      return;
    }
  tkeyp = temp_key_int (id);
  CLEAR_STRUCT (val);
  init_dbt_string (&val, safe_gconf_key_key (keypath));

  /*
   * BerkeleyDB is in flux and currently prints an error message
   * (Duplicate data items are not supported with sorted data) when a
   * key exists, even though a return code exists for this purpose. For
   * the moment, test if the key has an entry in DB_KEY first What's
   * worse, BDB will sometimes not get a key but complain that it
   * already exists!
   */
#if 0
  ret = bdb->dbkeyp->put (bdb->dbkeyp, NULL, tkeyp, &val, 0);
#else
  ret = bdb->dbkeyp->get (bdb->dbkeyp, NULL, tkeyp, &val, 0);
  if (ret != 0)
    {
      /* key does not exist - put it */
      ret = bdb->dbkeyp->put (bdb->dbkeyp, NULL, tkeyp, &val, 0);
    }
#endif

  if ((ret != 0) && (ret != DB_KEYEXIST))
    {
#ifdef GCONF_ERROR_VERBOSE
      fprintf (stderr, "%s: failed to put key-name '%s' in directory %d\n",
	       sysname, (char *) val.data, *(int *) tkeyp->data);
#endif
    }
}

static
entry_already_added (GSList * list, const char *key)
{
  while (list)
    {
      GConfEntry *entry = (GConfEntry *) list->data;

      if (strcmp (key, entry->key) == 0)
	return TRUE;
      list = g_slist_next (list);
    }
  return FALSE;
}

GSList *
bdb_all_entries (BDB_Store * bdb, const char *dirpath,
		 GSList * list, GError ** err)
{
  int ret;
  DBT keydir;
  DBT key;
  DBT value;
  int mode;
  char keybuf[MAXPATHLEN * 2];
  GConfEntry entry;
  int dirid = get_dir_id (bdb, dirpath);
  CLEAR_STRUCT (keydir);
  CLEAR_STRUCT (key);
  CLEAR_STRUCT (value);
  *err = NULL;
  /* fetch all regular and schema entries from the given directory */
  keydir.size = sizeof (dirid);
  keydir.data = &dirid;
  if (strcmp (dirpath, ROOT) == 0)
    dirpath = "";
  /* find all keys in this directory and get their values */
  mode = DB_SET;
  while ((ret = bdb->keycp->c_get (bdb->keycp, &keydir, &key, mode)) == 0)
    {
      sprintf (keybuf, "%s/%s", dirpath, (char *) key.data);
      if (entry_already_added (list, keybuf))
	continue;
      ret =
	bdb->dbvalp->get (bdb->dbvalp, NULL, temp_key_string (keybuf),
			  &value, 0);
      if (ret == 0)
	{
	  entry.is_default = FALSE;
	  entry.key = strdup (keybuf);
	  entry.schema_name = NULL;
	  entry.value = bdb_restore_value ((char *) value.data);
	}
      else
	{
	  continue;
	}
      list = g_slist_append (list, struct_dup (entry));
      mode = DB_NEXT_DUP;
      CLEAR_STRUCT (key);
    }
  /* find all keys in this (schema) directory and get their values */
  mode = DB_SET;
  while (
	 (ret =
	  bdb->schkeycp->c_get (bdb->schkeycp, &keydir, &key, mode)) == 0)
    {
      sprintf (keybuf, "%s/%s", dirpath, (char *) key.data);
      if (entry_already_added (list, keybuf))
	continue;
      entry.schema_name = get_schema_key (bdb, keybuf);
      ret =
	bdb->dbschp->get (bdb->dbschp, NULL,
			  temp_key_string (entry.schema_name), &value, 0);
      if (ret == 0)
	{
	  entry.is_default = TRUE;
	  entry.key = strdup (keybuf);
	  entry.value = bdb_restore_value ((char *) value.data);
	}
      else
	{
	  free (entry.schema_name);
	  continue;
	}
      list = g_slist_append (list, struct_dup (entry));
      mode = DB_NEXT_DUP;
      CLEAR_STRUCT (key);
    }
  return list;
}

GSList *
bdb_all_dirs_into_list (BDB_Store * bdb,
			const char *dirname, GSList * list, GError ** err)
{
  DBT dir;
  DBT dirid;
  DBT hier;
  int ret;
  int mode;
  DBC *hiercp;
  *err = NULL;			/* TODO: set err appropriately */
  CLEAR_STRUCT (dir);
  CLEAR_STRUCT (dirid);
  CLEAR_STRUCT (hier);
  dir.data = (void *) dirname;
  dir.size = strlen (dirname) + 1;
  if ((ret = bdb->dbdirp->get (bdb->dbdirp, NULL, &dir, &dirid, 0)) != 0)
    {

#ifdef GCONF_ERROR_VERBOSE
      fprintf (stderr, "ERROR: Failed to find directory '%s'\n", dirname);
#endif
      return NULL;
    }
  if (bdb->dbhierp->cursor (bdb->dbhierp, NULL, &hiercp, 0) != 0)
    {
#ifdef GCONF_ERROR_VERBOSE
      fprintf (stderr, "ERROR: Failed to open hierarchy cursor\n");
#endif
      return NULL;
    }
  mode = DB_SET;
  while ((ret = hiercp->c_get (hiercp, &dirid, &hier, mode)) == 0)
    {
      if (bdb_is_localised ((char *) hier.data))
	continue;
      list = g_slist_append (list, strdup ((char *) hier.data));
      CLEAR_STRUCT (hier);
      mode = DB_NEXT_DUP;
    }
  close_cursor_or_error (bdb->dbhierp, hiercp, DBD_HIERARCHY);
  return list;
}

GSList *
bdb_all_dirs_into_list_recursive (BDB_Store * bdb,
				  const char
				  *dirname, GSList * list, GError ** err)
{
  DBT dir;
  DBT dirid;
  DBT hier;
  int ret;
  int mode;
  DBC *hiercp;
  char dirbuf[MAXPATHLEN * 2];
  *err = NULL;			/* TODO: set err appropriately */
  CLEAR_STRUCT (dir);
  CLEAR_STRUCT (dirid);
  CLEAR_STRUCT (hier);
  init_dbt_string (&dir, dirname);
  if ((ret = bdb->dbdirp->get (bdb->dbdirp, NULL, &dir, &dirid, 0)) != 0)
    {

#ifdef GCONF_ERROR_VERBOSE
      fprintf (stderr,
	       "%s: bdb_all_dirs_into_list_recursive - failed to find directory '%s'\n",
	       sysname, dirname);
#endif
      return NULL;
    }
  if (bdb->dbhierp->cursor (bdb->dbhierp, NULL, &hiercp, 0) != 0)
    {

#ifdef GCONF_ERROR_VERBOSE
      fprintf (stderr,
	       "%s: bdb_all_dirs_into_list_recursive - failed to open hierarchy cursor for '%s'\n",
	       sysname, dirname);
#endif
      return NULL;
    }
  if (strcmp (dirname, "/") == 0)
    {
      dirname = "";		/* make sprintf call below simple for root
				 * case also */
    }
  mode = DB_SET;
  while ((ret = hiercp->c_get (hiercp, &dirid, &hier, mode)) == 0)
    {
      if (bdb_is_localised ((char *) hier.data))
	continue;
      sprintf (dirbuf, "%s/%s", dirname, (char *) hier.data);
      list = g_slist_append (list, strdup (dirbuf));
      list = bdb_all_dirs_into_list_recursive (bdb, dirbuf, list, err);
      mode = DB_NEXT_DUP;
      CLEAR_STRUCT (hier);
    }
  close_cursor_or_error (bdb->dbhierp, hiercp, DBD_HIERARCHY);
  return list;
}

GConfValue *
bdb_get_value (BDB_Store * bdb, const char *key)
{
  DBT value;
  int ret;
  g_assert (bdb != 0);
  g_assert (key != 0);
  CLEAR_STRUCT (value);
  ret =
    bdb->dbvalp->get (bdb->dbvalp, NULL, temp_key_string (key), &value, 0);
  if (ret == 0)
    {
      return bdb_restore_value ((char *) value.data);
    }
#ifdef GCONF_ENABLE_BDB_DEBUG
  fprintf (stderr, "%s: bdb_get_value(%s) returned %d\n", sysname, key, ret);
#endif
  return NULL;
}

static char *
get_schema_key (BDB_Store * bdb, const char *key)
{
  int ret;
  DBT skey, value;

  CLEAR_STRUCT (skey);
  CLEAR_STRUCT (value);
  /* fetch the schema name */
  init_dbt_string (&skey, key);
  if ((ret = bdb->dbschp->get (bdb->dbschp, NULL, &skey, &value, 0)) == 0)
    {
#ifdef GCONF_ENABLE_BDB_DEBUG
      fprintf (stderr, "%s: found schema %s for key %s\n",
	       sysname, (char *) value.data, key);
#endif
      return strdup ((char *) value.data);
    }
#ifdef GCONF_ENABLE_BDB_DEBUG
  fprintf (stderr, "%s: found no schema for key %s\n", sysname, key);
#endif
  return NULL;
}

static GConfValue *
get_schema_default_value (BDB_Store * bdb, const char *schema_key)
{
  if (schema_key)
    {
      GConfValue *schema_value = bdb_get_value (bdb, schema_key);
      if (schema_value == NULL)
	{
	  fprintf (stderr, "%s: no schema value for schema key %s\n",
		   sysname, schema_key);
	}
      else
	{
	  return gconf_value_get_schema (schema_value)->default_value;
	}
    }
  return NULL;
}

GConfValue *
bdb_query_value (BDB_Store * bdb,
		 const char *key, char **schema_namep, GError ** err)
{
  GConfValue *value;
  char *schema_key;
  *err = NULL;
  if (schema_namep != NULL)
    {
      *schema_namep = get_schema_key (bdb, key);
    }
  value = bdb_get_value (bdb, key);
#ifdef BACKEND_RETURNS_DEFAULT
  if (value == NULL)
    {
      GConfValue *schema_value;
      schema_key = schema_namep ? *schema_namep : get_schema_key (bdb, key);
      value = get_schema_default_value (bdb, schema_key);
    }
#endif
  return value;
}

void
bdb_put_value (BDB_Store * bdb, const char *key,
	       GConfValue * val, GError ** err)
{
  size_t len;
  char *buf = bdb_serialize_value (val, &len);

  g_return_if_fail (buf != NULL);
#ifdef GCONF_ENABLE_BDB_DEBUG
  fprintf (stderr, "%s: key %s serialized as '%s'\n", sysname, key, buf);
#endif
  put_key (bdb, key, buf, len);
  _gconf_check_free (buf);
}

void
bdb_unset_value (BDB_Store * bdb, const char *keypath,
		 const char *locale, GError ** err)
{
  int mode;
  guint32 flags = 0;
  int ret;
  DBT key;
  DBT value;
  DBT keydir;

  *err = NULL;
  CLEAR_STRUCT (key);
  CLEAR_STRUCT (value);
  CLEAR_STRUCT (keydir);
  init_dbt_string (&key, keypath);
  if ((ret = bdb->dbvalp->get (bdb->dbvalp, NULL, &key, &value, flags)) == 0)
    {
      guint32 dirid;
      char *dirpath;
      const char *keyname = gconf_key_key (keypath);
      /* delete the value */
      bdb->dbvalp->del (bdb->dbvalp, NULL, &key, flags);
      /* TODO: error handling! */
      /*
       * The value was found - now find and delete the key-name
       * from the directory's key-list
       */
      dirpath = parent_of (keypath);
      dirid = get_dir_id (bdb, dirpath);
      free_dir (dirpath);
      init_dbt_int (&keydir, &dirid);
      mode = DB_SET;
      while ((ret = bdb->keycp->c_get (bdb->keycp, &keydir, &key, mode)) == 0)
	{
	  if (strcmp ((char *) key.data, keyname) == 0)
	    {
	      bdb->keycp->c_del (bdb->keycp, flags);
	      /* TODO: error handling! */
	      return;		/* success */
	    }
	  CLEAR_STRUCT (key);
	  mode = DB_NEXT_DUP;
	}
    }
}

gboolean
bdb_dir_exists (BDB_Store * bdb, const char *dir, GError * err)
{
  int ret;
  DBT value;
  CLEAR_STRUCT (value);
  ret =
    bdb->dbdirp->get (bdb->dbdirp, NULL, temp_key_string (dir), &value, 0);
  return ret ? FALSE : TRUE;
}

void
bdb_remove_entries (BDB_Store * bdb, const char *dirpath, GError ** err)
{
  int ret;
  DBT keydir;
  DBT key;
  DBT value;
  int mode;
  guint32 dirid = get_dir_id (bdb, dirpath);
  guint32 flags = 0;

  CLEAR_STRUCT (key);
  CLEAR_STRUCT (value);
  CLEAR_STRUCT (keydir);
  *err = NULL;
  keydir.size = sizeof (dirid);
  keydir.data = &dirid;
  mode = DB_SET;
  while ((ret = bdb->keycp->c_get (bdb->keycp, &keydir, &key, mode)) == 0)
    {
      char dirbuf[MAXPATHLEN * 2];
      sprintf (dirbuf, "%s/%s", dirpath, (char *) key.data);
      init_dbt_string (&value, dirbuf);
      /* TODO: error handling! */
      bdb->dbvalp->del (bdb->dbvalp, NULL, &value, flags);
      bdb->keycp->c_del (bdb->keycp, flags);
      mode = DB_NEXT_DUP /* | DB_RMW */ ;
      CLEAR_STRUCT (key);
    }
}

void
bdb_remove_dir (BDB_Store * bdb, const char *dirname, GError ** err)
{
  DBT dir;
  DBT dirid;
  DBT hier;
  int ret;
  int mode;
  DBC *hiercp;

  *err = NULL;			/* TODO: set err appropriately */
  CLEAR_STRUCT (dir);
  CLEAR_STRUCT (dirid);
  CLEAR_STRUCT (hier);
  dir.data = (void *) dirname;
  dir.size = strlen (dirname) + 1;
  if ((ret = bdb->dbdirp->get (bdb->dbdirp, NULL, &dir, &dirid, 0)) != 0)
    {
#ifdef GCONF_ERROR_VERBOSE
      fprintf (stderr,
	       "GConf(bdb): bdb_remove_dir - failed to find directory '%s'\n",
	       dirname);
#endif
      return;
    }
  bdb_remove_entries (bdb, dirname, err);
  if (bdb->dbhierp->cursor (bdb->dbhierp, NULL, &hiercp, 0) != 0)
    {
#ifdef GCONF_ERROR_VERBOSE
      fprintf (stderr, "%s: Failed to open hierarchy cursor\n", sysname);
#endif
      return;
    }
  mode = DB_SET;
  while ((ret = hiercp->c_get (hiercp, &dirid, &hier, mode)) == 0)
    {
      char dirbuf[MAXPATHLEN * 2];
      sprintf (dirbuf, "%s/%s", dirname, (char *) hier.data);
      bdb_remove_dir (bdb, dirbuf, err);
      mode = DB_NEXT_DUP;
      CLEAR_STRUCT (hier);
    }
  close_cursor_or_error (bdb->dbhierp, hiercp, DBD_HIERARCHY);
  bdb->dbdirp->del (bdb->dbdirp, NULL, &dir, 0);
}

GSList *
bdb_all_subdirs (BDB_Store * bdb, const char *dirname, GError ** err)
{
  return bdb_all_dirs_into_list (bdb, dirname, NULL, err);
}

static int
del_key_value_pair (DB * dbp, DBT * key, DBT * value)
{
  int ret;
  DBC *dbcurp;
  int mode;
  DBT found;

  if (dbp->cursor (dbp, NULL, &dbcurp, 0) != 0)
    {
#ifdef GCONF_ERROR_VERBOSE
      fprintf (stderr, "%s: failed to create cursor in del_key_value_pair\n",
	       sysname);
#endif
    }
  mode = DB_SET;
  while ((ret = dbcurp->c_get (dbcurp, key, &found, mode)) == 0)
    {
      if ((found.size == value->size)
	  && (memcmp (found.data, value->data, found.size) == 0))
	{
	  ret = dbcurp->c_del (dbcurp, 0);
	  break;

	}
      mode = DB_NEXT_DUP;
      CLEAR_STRUCT (value);
    }
  close_cursor_or_error (dbp, dbcurp, "Unknown");
  return ret;
}

void
bdb_set_schema (BDB_Store * bdb, const char *key,
		const char *schema_key, GError ** err)
{
  int ret;
  DBT skey;
  char *parent;
  int parent_id;
  char *localised_key;

  CLEAR_STRUCT (skey);
  *err = NULL;

  if (!schema_key || (*schema_key == '\0') || (strcmp (schema_key, "/") == 0))
    {
      char *tkey;

      /*
       * NOTE: no explicit way to disassociate a schema from a key
       * - use a NULL or empty string to signify breaking an
       * association.
       */
      ret =
	bdb->dbschp->get (bdb->dbschp, NULL, temp_key_string (key), &skey, 0);
      if (ret != 0)
	{
	  /* no association exists - nothing to do */
	  return;
	}
      /*
       * an association exists - copy the key (non-dir) portion of
       * the schema key
       */
      tkey = strdup (gconf_key_key ((char *) skey.data));
      ret = bdb->dbschp->del (bdb->dbschp, NULL, temp_key_string (key), 0);
      init_dbt_string (&skey, tkey);

      ret +=
	del_key_value_pair (bdb->dbschkeyp, temp_key_int (parent_id), &skey);
      free (tkey);
      if (ret != 0)
	{
#ifdef GCONF_ERROR_VERBOSE
	  fprintf (stderr,
		   "%s: failed to disassociate schema %s from key %s\n",
		   sysname, schema_key, key);
#endif
	  return;
	}
    }
  else
    {
      parent = parent_of (key);
      parent_id = get_or_create_dir (bdb, parent);
      if (parent_id == INVALID_DIR_ID)
	return;
      free_dir (parent);
      init_dbt_string (&skey, gconf_key_key (schema_key));
      ret =
	bdb->dbschkeyp->put (bdb->dbschkeyp, NULL,
			     temp_key_int (parent_id), &skey, 0);
      if (ret != 0)
	{
#ifdef GCONF_ERROR_VERBOSE
	  fprintf (stderr,
		   "%s: failed to associate schema %s (key=%s) with dir (%d)\n",
		   sysname, schema_key, key, parent_id);
#endif
	  return;
	}
      init_dbt_string (&skey, schema_key);
      ret =
	bdb->dbschp->put (bdb->dbschp, NULL, temp_key_string (key), &skey, 0);
      if (ret != 0)
	{
#ifdef GCONF_ERROR_VERBOSE
	  fprintf (stderr,
		   "%s: failed to associate schema %s with key %s\n",
		   sysname, schema_key, key);
#endif
	  return;
	}
#ifdef GCONF_ENABLE_BDB_DEBUG
      fprintf (stderr, "%s: associated schema %s with key %s\n",
	       sysname, schema_key, key);
      schema_key = get_schema_key (bdb, key);
      fprintf (stderr, "%s: verified schema %s with key %s\n",
	       sysname, schema_key ? schema_key : "<NULL>", key);
#endif
    }
}
