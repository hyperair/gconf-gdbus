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

/*
 * Overview
 *
 * A GConf back-end is a storage implementation for the GConf API. Different
 * back-ends or "drivers" are integrated into GConf using a struct
 * GConfBackendVTable containing function pointers, simulating a C++
 * virtual function pointer table (vtable).
 *
 * This back-end uses the BerkeleyDB database toolkit from Sleepycat
 * software (www.sleepycat.com). BerkeleyDB is not a relational database,
 * but is highly suited to GConf as it simply stores key-value pairs and
 * is relatively efficient.
 *
 * The storage model if implemented in a relational database would
 * use two tables:
 *
 *   Directory structure table: { directory_path, id, parent_directory_id }
 *   Keyname/Value table:       { key_name, directory_id, value }
 *
 * A keyname is the last element in the hierarchical key; a directory path
 * is required to form the complete key - this composition of directory path
 * and keyname is called a keypath.
 *
 * Because a BerkeleyDB database can only store a single key+value pair,
 * this program splits these tables into four databases:
 *
 * Table:        Key | Value:                          Data?
 * -----         -----------                           ----
 * BDB_DIR       directory_path | directory_id         Yes
 * BDB_HIERARCHY directory_id | child_directory_name   No
 * BDB_KEY       directory_id | keyname                No
 * BDB_VALUE     keypath | value                       Yes
 * BDB_SCHEMA    keypath | schema-key                  Yes
 * BDB_SCHKEY    directory_id | schema-key-name        No
 *
 * A Data table is one that may not have records with identical keys. Other
 * tables may not contain identical key-value pairs (i.e. two records
 * could have the same key but must have different values).
 *

 * Note that an important goal is that all tables could be
 * reconstructed from the BDB_VALUE table and the BDB_SCHEMA table.  This
 * is possible because the BDB_VALUE table contains the full key-name (the
 * keypath) and the value, similar to the BDB_SCHEMA table; the other
 * tables permit the database to be traversed hierarchically instead of
 * linearly.
 *
 * Locales
 * 
 * Locale data is stored in a subdirectory of the regular key directory for each
 * locale; e.g. the Spanish locale data for a key /gnome/desktop/logo is
 * /gnome/desktop/%locale%es/logo. This allows locale data to be found
 * efficiently, e.g. the all_entries() backend method accepts a list of
 * locales to be searched. If the list contains, { "es", "en", "C" }
 * then the directories searched are /gnome/desktop/, /gnome/desktop/%locale%es,
 * /gnome/desktop/%locale%en, and /gnome/desktop/%locale%C.
 *
 * Schemas
 *
 * Schemas are stored with regular keys in the BDB_VALUE database, but it
 * is necessary to search for them specifically. This requires two databases,
 * BDB_SCHEMA to store the "applyto" key (see the GConf docs) and the schema
 * key under which the schema can be found. The BDB_SCHKEY is similar to the
 * BDB_KEY database as it records the keys in each directory for which a 
 * schema value may be found.
 *
 * Locking
 *
 * GConf doesn't specify any locking functionality at the API level.
 * Currently locking is a requirement, because workgroups and ultimately
 * enterprises will require large central GConf databases to store
 * common settings and defaults for users, although these users will
 * also require separate writable stores for their personal preferences.
 * For now, no locking functionality is implemented, but later locking may
 * be required for individual key-value pairs and for hierarchies. The
 * BDB_DIR database is locked when updating the highest integer id value
 * assigned to a directory.
 *
 * Caching
 *
 * Ideally caching should be independent of any back-end, which can only
 * be achieved by layering the cache on top of the back-ends rather than
 * within each one, though this is not always practical/efficient. This
 * implementation includes minimal caching for database structure, not
 * for values. Even this caching should be above the back-ends, as it
 * is time-consuming to find a key and to decide where it should be
 * stored; if each store in GConf has a base path, then no queries are
 * needed at run-time to locate keys for read or write.
 *
 * Atomic Saving
 *
 * BerkeleyDB transactions are used to do grouped writes.
 */

#ifndef BDB_H

#ifdef HAVE_DB3_DB_H
#include <db3/db.h>
#else
#include <db.h>
#endif
#include <glib.h>
#include <gconf/gconf.h>

#define	DBD_DIR	"dir.db"
#define	DBD_HIERARCHY	"hierarchy.db"
#define	DBD_KEY	"key.db"
#define	DBD_VALUE	"value.db"
#define	DBD_SCHEMA	"schema.db"
#define	DBD_SCHKEY	"schkey.db"

struct _BDB_Store;

typedef struct _BDB_Store
{
  DB *dbdirp;
  DB *dbhierp;
  DB *dbkeyp;
  DB *dbvalp;
  DB *dbschp;
  DB *dbschkeyp;

  DBC *keycp;
  DBC *schkeycp;
}
BDB_Store;

int bdb_create (BDB_Store * bdb, const char *dbname);

BDB_Store *bdb_new (const char *dir, int flags);

#define CLEAR_STRUCT(x) (memset(&x, 0, sizeof(x)))

extern DBT *temp_string_key (const char *key);
extern DBT *temp_int_key (int akey);
extern guint32 get_dir_id (BDB_Store * bdb, const char *dir);
extern void add_key (BDB_Store * bdb, const char *dir, const char *keypath);
extern void bdb_set_sysname (const char *name);
extern guint32 get_or_create_dir (BDB_Store * bdb, const char *dir);

int bdb_create (BDB_Store * bdb, const char *dir);
int bdb_open (BDB_Store * bdb, const char *dir, int flags);
void bdb_close (BDB_Store * bdb);

extern GConfValue *bdb_query_value (BDB_Store * bdb, const char *key,
				    char **schema_name, GError ** err);

extern GSList *bdb_all_dirs (BDB_Store * bdb, const char *dirname,
			     GError ** err);
extern GSList *bdb_all_entries (BDB_Store * bdb, const char *dirpath,
				GSList * inlist, GError ** err);
extern void bdb_put_value (BDB_Store * bdb, const char *key, GConfValue * val,
			   GError ** err);
extern GSList *bdb_all_subdirs (BDB_Store * bdb, const char *dirname,
				GError ** err);
extern void bdb_unset_value (BDB_Store * bdb, const char *keypath,
			     const char *locale, GError ** err);
extern void bdb_remove_dir (BDB_Store * bdb, const char *dirname,
			    GError ** err);
extern void bdb_set_schema (BDB_Store * bdb, const char *key,
			    const char *schema_key, GError ** err);

extern gboolean bdb_is_localised (const gchar * key);

#define struct_dup(x) g_memdup(&x, sizeof(x))

#endif /* BDB_H */
