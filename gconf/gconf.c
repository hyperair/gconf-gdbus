/* GConf
 * Copyright (C) 1999, 2000 Red Hat Inc.
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

#include <popt.h>
#include "GConf.h"
#include "gconf.h"
#include "gconf-internals.h"
#include "gconf-sources.h"
#include "gconf-locale.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <unistd.h>

/* Returns TRUE if there was an error, frees exception, sets err */
static gboolean gconf_handle_corba_exception(CORBA_Environment* ev, GError** err);
/* just returns TRUE if there's an exception indicating the server is
   probably hosed; no side effects */
static gboolean gconf_server_broken(CORBA_Environment* ev);

/* Maximum number of times to try re-spawning the server if it's down. */
#define MAX_RETRIES 1

gboolean
gconf_key_check(const gchar* key, GError** err)
{
  gchar* why = NULL;

  if (!gconf_valid_key(key, &why))
    {
      if (err)
        *err = gconf_error_new(GCONF_ERROR_BAD_KEY, _("`%s': %s"),
                               key, why);
      g_free(why);
      return FALSE;
    }
  return TRUE;
}

typedef struct _CnxnTable CnxnTable;

struct _GConfEngine {
  guint refcount;

  ConfigDatabase database;

  CnxnTable* ctable;

  /* If non-NULL, this is a local engine;
     local engines don't do notification! */
  GConfSources* local_sources;
  
  /* An address if this is not the default engine;
   * NULL if it's the default
   */
  gchar *address;

  gpointer user_data;
  GDestroyNotify dnotify;
  
  guint is_default : 1;

  /* If TRUE, this is a local engine (and therefore
   * has no ctable and no notifications)
   */
  guint is_local : 1;
};

typedef struct _GConfCnxn GConfCnxn;

struct _GConfCnxn {
  gchar* namespace_section;
  guint client_id;
  CORBA_unsigned_long server_id; /* id returned from server */
  GConfEngine* conf;             /* engine we're associated with */
  GConfNotifyFunc func;
  gpointer user_data;
};

static GConfEngine *default_engine = NULL;

static GConfCnxn* gconf_cnxn_new     (GConfEngine         *conf,
                                      const gchar         *namespace_section,
                                      CORBA_unsigned_long  server_id,
                                      GConfNotifyFunc      func,
                                      gpointer             user_data);
static void       gconf_cnxn_destroy (GConfCnxn           *cnxn);
static void       gconf_cnxn_notify  (GConfCnxn           *cnxn,
                                      GConfEntry          *entry);


static ConfigServer   gconf_get_config_server    (gboolean     start_if_not_found,
                                                  GError **err);

/* Forget our current server object reference, so the next call to
   gconf_get_config_server will have to try to respawn the server */
static void           gconf_detach_config_server (void);
static ConfigListener gconf_get_config_listener  (void);

static void           gconf_engine_detach       (GConfEngine     *conf);
static gboolean       gconf_engine_connect      (GConfEngine     *conf,
                                                 gboolean         start_if_not_found,
                                                 GError         **err);
static void           gconf_engine_set_database (GConfEngine     *conf,
                                                 ConfigDatabase   db);
static ConfigDatabase gconf_engine_get_database (GConfEngine     *conf,
                                                 gboolean         start_if_not_found,
                                                 GError         **err);


static void         register_engine           (GConfEngine    *conf);
static void         unregister_engine         (GConfEngine    *conf);
static GConfEngine *lookup_engine             (const gchar    *address);
static GConfEngine *lookup_engine_by_database (ConfigDatabase  db);


/* We'll use client-specific connection numbers to return to library
   users, so if gconfd dies we can transparently re-register all our
   listener functions.  */

struct _CnxnTable {
  /* Hash from server-returned connection ID to GConfCnxn */
  GHashTable* server_ids;
  /* Hash from our connection ID to GConfCnxn */
  GHashTable* client_ids;
};

static CnxnTable* ctable_new                 (void);
static void       ctable_destroy             (CnxnTable           *ct);
static void       ctable_insert              (CnxnTable           *ct,
                                              GConfCnxn           *cnxn);
static void       ctable_remove              (CnxnTable           *ct,
                                              GConfCnxn           *cnxn);
static void       ctable_remove_by_client_id (CnxnTable           *ct,
                                              guint                client_id);
static GSList*    ctable_remove_by_conf      (CnxnTable           *ct,
                                              GConfEngine         *conf);
static GConfCnxn* ctable_lookup_by_client_id (CnxnTable           *ct,
                                              guint                client_id);
static GConfCnxn* ctable_lookup_by_server_id (CnxnTable           *ct,
                                              CORBA_unsigned_long  server_id);
static void       ctable_reinstall           (CnxnTable           *ct,
                                              GConfCnxn           *cnxn,
                                              guint                old_server_id,
                                              guint                new_server_id);


static GConfEngine*
gconf_engine_blank (gboolean remote)
{
  GConfEngine* conf;

  conf = g_new0(GConfEngine, 1);

  conf->refcount = 1;
  
  if (remote)
    {
      conf->database = CORBA_OBJECT_NIL;
      conf->ctable = ctable_new();
      conf->local_sources = NULL;
      conf->is_local = FALSE;
      conf->is_default = TRUE;
    }
  else
    {
      conf->database = CORBA_OBJECT_NIL;
      conf->ctable = NULL;
      conf->local_sources = NULL;
      conf->is_local = TRUE;
      conf->is_default = FALSE;
    }
    
  return conf;
}

static GHashTable *engines_by_db = NULL;

static GConfEngine *
lookup_engine_by_database (ConfigDatabase db)
{
  if (engines_by_db)
    return g_hash_table_lookup (engines_by_db, db);
  else
    return NULL;
}

/* This takes ownership of the ConfigDatabase */
static void
gconf_engine_set_database (GConfEngine *conf,
                           ConfigDatabase db)
{
  gconf_engine_detach (conf);

  conf->database = db;

  if (engines_by_db == NULL)
    engines_by_db = g_hash_table_new ((GHashFunc) gconf_CORBA_Object_hash,
                                      (GCompareFunc) gconf_CORBA_Object_equal);
  
  g_hash_table_insert (engines_by_db, conf->database, conf);  
}

static void
gconf_engine_detach (GConfEngine *conf)
{
  CORBA_Environment ev;

  CORBA_exception_init (&ev);
  
  if (!CORBA_Object_is_nil (conf->database, &ev))
    {
      g_hash_table_remove (engines_by_db, conf->database);
      
      CORBA_Object_release (conf->database, &ev);
      conf->database = CORBA_OBJECT_NIL;
    }
}

static gboolean
gconf_engine_connect (GConfEngine *conf,
                      gboolean start_if_not_found,
                      GError **err)
{
  ConfigServer cs;
  ConfigDatabase db;
  int tries = 0;
  CORBA_Environment ev;
  
  g_return_val_if_fail (!conf->is_local, TRUE);
  
  CORBA_exception_init(&ev);

  if (!CORBA_Object_is_nil (conf->database, &ev))
    return TRUE;
  
 RETRY:
      
  cs = gconf_get_config_server(start_if_not_found, err);
      
  if (cs == CORBA_OBJECT_NIL)
    return FALSE; /* Error should already be set */

  if (conf->is_default)
    db = ConfigServer_get_default_database (cs, &ev);      
  else
    db = ConfigServer_get_database (cs, conf->address, &ev);
  
  if (gconf_server_broken(&ev))
    {
      if (tries < MAX_RETRIES)
        {
          ++tries;
          CORBA_exception_free(&ev);
          gconf_detach_config_server();
          goto RETRY;
        }
    }
  
  if (gconf_handle_corba_exception(&ev, err))
    return FALSE;

  if (CORBA_Object_is_nil (db, &ev))
    {
      if (err)
        *err = gconf_error_new(GCONF_ERROR_BAD_ADDRESS,
                               _("Server couldn't resolve the address `%s'"),
                               conf->address ? conf->address : "default");
          
      return FALSE;
    }

  gconf_engine_set_database (conf, db);
  
  return TRUE;
}

static ConfigDatabase
gconf_engine_get_database (GConfEngine *conf,
                           gboolean start_if_not_found,
                           GError **err)
{
  if (!gconf_engine_connect (conf, start_if_not_found, err))
    return CORBA_OBJECT_NIL;
  else
    return conf->database;
}

static gboolean
gconf_engine_is_local(GConfEngine* conf)
{
  return conf->is_local;
}

static GHashTable *engines_by_address = NULL;

static void
register_engine (GConfEngine *conf)
{
  g_return_if_fail (conf->address != NULL);

  if (engines_by_address == NULL)
    engines_by_address = g_hash_table_new (g_str_hash, g_str_equal);

  g_hash_table_insert (engines_by_address, conf->address, conf);
}

static void
unregister_engine (GConfEngine *conf)
{
  g_return_if_fail (conf->address != NULL);
  g_return_if_fail (engines_by_address != NULL);
  
  g_hash_table_remove (engines_by_address, conf->address);

  if (g_hash_table_size (engines_by_address) == 0)
    {
      g_hash_table_destroy (engines_by_address);
      
      engines_by_address = NULL;
    }
}

static GConfEngine *
lookup_engine (const gchar *address)
{
  if (engines_by_address)
    return g_hash_table_lookup (engines_by_address, address);
  else
    return NULL;
}


/*
 *  Public Interface
 */

GConfEngine*
gconf_engine_get_local      (const gchar* address,
                             GError** err)
{
  GConfEngine* conf;
  GConfSource* source;

  g_return_val_if_fail(address != NULL, NULL);
  g_return_val_if_fail(err == NULL || *err == NULL, NULL);
  
  source = gconf_resolve_address(address, err);

  if (source == NULL)
    return NULL;
  
  conf = gconf_engine_blank(FALSE);

  conf->local_sources = gconf_sources_new_from_source(source);

  g_assert (gconf_engine_is_local (conf));
  
  return conf;
}

GConfEngine*
gconf_engine_get_default (void)
{
  GConfEngine* conf = NULL;
  
  if (default_engine)
    conf = default_engine;

  if (conf == NULL)
    {
      conf = gconf_engine_blank(TRUE);

      conf->is_default = TRUE;

      default_engine = conf;
      
      /* Ignore errors, we never return a NULL
       * default database
       */
      gconf_engine_connect (conf, TRUE, NULL);
    }
  else
    conf->refcount += 1;
  
  return conf;
}

GConfEngine*
gconf_engine_get_for_address (const gchar* address, GError** err)
{
  GConfEngine* conf;
  
  g_warning("Non-default configuration sources currently do not support change-notification, and are not yet recommended for use in applications.");

  conf = lookup_engine (address);

  if (conf == NULL)
    {
      conf = gconf_engine_blank(TRUE);

      conf->is_default = FALSE;
      conf->address = g_strdup (address);

      if (!gconf_engine_connect (conf, TRUE, err))
        {
          gconf_engine_unref (conf);
          return NULL;
        }

      register_engine (conf);
    }
  else
    conf->refcount += 1;
  
  return conf;
}

void
gconf_engine_ref             (GConfEngine* conf)
{
  g_return_if_fail(conf != NULL);
  g_return_if_fail(conf->refcount > 0);

  conf->refcount += 1;
}

void         
gconf_engine_unref        (GConfEngine* conf)
{
  g_return_if_fail(conf != NULL);
  g_return_if_fail(conf->refcount > 0);

  conf->refcount -= 1;
  
  if (conf->refcount == 0)
    {
      if (gconf_engine_is_local(conf))
        {
          if (conf->local_sources != NULL)
            gconf_sources_free(conf->local_sources);
        }
      else
        {
          /* Remove all connections associated with this GConf */
          GSList* removed;
          GSList* tmp;
          CORBA_Environment ev;
      
          CORBA_exception_init(&ev);

          /* FIXME CnxnTable only has entries for this GConfEngine now,
           * it used to be global and shared among GConfEngine objects.
           */
          removed = ctable_remove_by_conf (conf->ctable, conf);
  
          tmp = removed;
          while (tmp != NULL)
            {
              GConfCnxn* gcnxn = tmp->data;

              if (!CORBA_Object_is_nil (conf->database, &ev))
                {
                  GError* err = NULL;
              
                  ConfigDatabase_remove_listener(conf->database,
                                                 gcnxn->server_id,
                                                 &ev);

                  if (gconf_handle_corba_exception(&ev, &err))
                    {
                      /* Don't set error because realistically this
                         doesn't matter to clients */
#ifdef GCONF_ENABLE_DEBUG
                      g_warning("Failure removing listener %u from the config server: %s",
                                (guint)gcnxn->server_id,
                                err->message);
#endif
                    }
                }

              gconf_cnxn_destroy(gcnxn);

              tmp = g_slist_next(tmp);
            }

          g_slist_free(removed);

          if (conf->dnotify)
            {
              (* conf->dnotify) (conf->user_data);
            }
          
          /* do this after removing the notifications,
             to avoid funky race conditions */
          if (conf->address)
            unregister_engine (conf);

          /* Release the ConfigDatabase */
          gconf_engine_detach (conf);
          
          ctable_destroy (conf->ctable);
        }

      if (conf == default_engine)
        default_engine = NULL;

      g_free(conf);
    }
}

void
gconf_engine_set_user_data  (GConfEngine   *engine,
                             gpointer       data,
                             GDestroyNotify dnotify)
{
  if (engine->dnotify)
    {
      (* engine->dnotify) (engine->user_data);
    }

  engine->dnotify = dnotify;
  engine->user_data = data;
}

gpointer
gconf_engine_get_user_data  (GConfEngine   *engine)
{
  return engine->user_data;
}

guint
gconf_engine_notify_add(GConfEngine* conf,
                        const gchar* namespace_section,
                        GConfNotifyFunc func,
                        gpointer user_data,
                        GError** err)
{
  ConfigDatabase db;
  ConfigListener cl;
  gulong id;
  CORBA_Environment ev;
  GConfCnxn* cnxn;
  gint tries = 0;

  g_return_val_if_fail(!gconf_engine_is_local(conf), 0);
  
  if (gconf_engine_is_local(conf))
    {
      if (err)
        *err = gconf_error_new(GCONF_ERROR_LOCAL_ENGINE,
                               _("Can't add notifications to a local configuration source"));

      return 0;
    }
  
  CORBA_exception_init(&ev);

 RETRY:
  
  db = gconf_engine_get_database (conf, TRUE, err);

  if (db == CORBA_OBJECT_NIL)
    return 0;

  cl = gconf_get_config_listener ();
  
  /* Should have aborted the program in this case probably */
  g_return_val_if_fail(cl != CORBA_OBJECT_NIL, 0);

  id = ConfigDatabase_add_listener(db,
                                   (gchar*)namespace_section, 
                                   cl, &ev);
  
  if (gconf_server_broken(&ev))
    {
      if (tries < MAX_RETRIES)
        {
          ++tries;
          CORBA_exception_free(&ev);
          gconf_engine_detach (conf);
          goto RETRY;
        }
    }
  
  if (gconf_handle_corba_exception(&ev, err))
    return 0;

  cnxn = gconf_cnxn_new(conf, namespace_section, id, func, user_data);

  ctable_insert(conf->ctable, cnxn);

  return cnxn->client_id;
}

void         
gconf_engine_notify_remove(GConfEngine* conf,
                    guint client_id)
{
  GConfCnxn* gcnxn;
  CORBA_Environment ev;
  ConfigDatabase db;
  gint tries = 0;

  if (gconf_engine_is_local(conf))
    return;
  
  CORBA_exception_init(&ev);

 RETRY:
  
  db = gconf_engine_get_database (conf, TRUE, NULL);

  if (db == CORBA_OBJECT_NIL)
    return;

  gcnxn = ctable_lookup_by_client_id(conf->ctable, client_id);

  g_return_if_fail(gcnxn != NULL);

  ConfigDatabase_remove_listener(db,
                                 gcnxn->server_id,
                                 &ev);

  if (gconf_server_broken(&ev))
    {
      if (tries < MAX_RETRIES)
        {
          ++tries;
          CORBA_exception_free(&ev);
          gconf_engine_detach (conf);
          goto RETRY;
        }
    }
  
  if (gconf_handle_corba_exception(&ev, NULL))
    {
      ; /* do nothing */
    }
  

  /* We want to do this even if the CORBA fails, so if we restart gconfd and 
     reinstall listeners we don't reinstall this one. */
  ctable_remove(conf->ctable, gcnxn);

  gconf_cnxn_destroy(gcnxn);
}

GConfValue *
gconf_engine_get_fuller (GConfEngine *conf,
                         const gchar *key,
                         const gchar *locale,
                         gboolean use_schema_default,
                         gboolean *is_default_p,
                         gboolean *is_writable_p,
                         gchar   **schema_name_p,
                         GError **err)
{
  GConfValue* val;
  ConfigValue* cv;
  CORBA_Environment ev;
  ConfigDatabase db;
  gint tries = 0;
  CORBA_boolean is_default = FALSE;
  CORBA_boolean is_writable = TRUE;
  CORBA_char *corba_schema_name = NULL;
  
  g_return_val_if_fail(conf != NULL, NULL);
  g_return_val_if_fail(key != NULL, NULL);
  g_return_val_if_fail(err == NULL || *err == NULL, NULL);

  if (!gconf_key_check(key, err))
    return NULL;

  if (gconf_engine_is_local(conf))
    {
      gchar** locale_list;
      gboolean tmp_is_default = FALSE;
      gboolean tmp_is_writable = TRUE;
      gchar *tmp_schema_name = NULL;
      
      locale_list = gconf_split_locale(locale);
      
      val = gconf_sources_query_value(conf->local_sources,
                                      key,
                                      (const gchar**)locale_list,
                                      use_schema_default,
                                      &tmp_is_default,
                                      &tmp_is_writable,
                                      schema_name_p ? &tmp_schema_name : NULL,
                                      err);

      if (locale_list != NULL)
        g_strfreev(locale_list);
      
      if (is_default_p)
        *is_default_p = tmp_is_default;

      if (is_writable_p)
        *is_writable_p = tmp_is_writable;

      if (schema_name_p)
        *schema_name_p = tmp_schema_name;
      else
        g_free (tmp_schema_name);
      
      return val;
    }

  g_assert(!gconf_engine_is_local(conf));
  
  CORBA_exception_init(&ev);

 RETRY:
  
  db = gconf_engine_get_database (conf, TRUE, err);

  if (db == CORBA_OBJECT_NIL)
    {
      g_return_val_if_fail(err == NULL || *err != NULL, NULL);

      return NULL;
    }

  if (schema_name_p)
    *schema_name_p = NULL;


  corba_schema_name = NULL;
  cv = ConfigDatabase2_lookup_with_schema_name (db,
                                                (gchar*)key, (gchar*)
                                                (locale ? locale : gconf_current_locale()),
                                                use_schema_default,
                                                &corba_schema_name,
                                                &is_default,
                                                &is_writable,
                                                &ev);

  if (ev._major == CORBA_SYSTEM_EXCEPTION &&
      CORBA_exception_id (&ev) &&
      strcmp (CORBA_exception_id (&ev), "IDL:CORBA/BAD_OPERATION:1.0") == 0)
    {
      CORBA_exception_free (&ev);
      CORBA_exception_init (&ev);
      
      cv = ConfigDatabase_lookup_with_locale(db,
                                             (gchar*)key, (gchar*)
                                             (locale ? locale : gconf_current_locale()),
                                             use_schema_default,
                                             &is_default,
                                             &is_writable,
                                             &ev);
    }
  
  if (gconf_server_broken(&ev))
    {
      if (tries < MAX_RETRIES)
        {
          ++tries;
          CORBA_exception_free(&ev);
          gconf_engine_detach (conf);
          goto RETRY;
        }
    }
  
  if (gconf_handle_corba_exception(&ev, err))
    {
      /* NOTE: don't free cv since we got an exception! */
      return NULL;
    }
  else
    {
      val = gconf_value_from_corba_value(cv);
      CORBA_free(cv);

      if (is_default_p)
        *is_default_p = !!is_default;
      if (is_writable_p)
        *is_writable_p = !!is_writable;

      /* we can't get a null pointer through corba
       * so the server sent us an empty string
       */
      if (corba_schema_name && corba_schema_name[0] != '/')
        {
          CORBA_free (corba_schema_name);
          corba_schema_name = NULL;
        }

      if (schema_name_p)
        *schema_name_p = g_strdup (corba_schema_name);

      if (corba_schema_name)
        CORBA_free (corba_schema_name);
      
      return val;
    }
}


GConfValue *
gconf_engine_get_full (GConfEngine *conf,
                       const gchar *key,
                       const gchar *locale,
                       gboolean use_schema_default,
                       gboolean *is_default_p,
                       gboolean *is_writable_p,
                       GError **err)
{
  return gconf_engine_get_fuller (conf, key, locale, use_schema_default,
                                  is_default_p, is_writable_p,
                                  NULL, err);
}

GConfEntry*
gconf_engine_get_entry(GConfEngine* conf,
                       const gchar* key,
                       const gchar* locale,
                       gboolean use_schema_default,
                       GError** err)
{
  gboolean is_writable = TRUE;
  gboolean is_default = FALSE;
  GConfValue *val;
  GError *error;
  GConfEntry *entry;
  gchar *schema_name;

  schema_name = NULL;
  error = NULL;
  val = gconf_engine_get_fuller (conf, key, locale, use_schema_default,
                                 &is_default, &is_writable,
                                 &schema_name, &error);
  if (error != NULL)
    {
      g_propagate_error (err, error);
      return NULL;
    }

  entry = gconf_entry_new_nocopy (g_strdup (key),
                                  val);
  
  entry->is_default = is_default;
  entry->is_writable = is_writable;
  entry->schema_name = schema_name; /* transfer memory ownership */

  return entry;
}
     
GConfValue*  
gconf_engine_get (GConfEngine* conf, const gchar* key, GError** err)
{
  return gconf_engine_get_with_locale(conf, key, NULL, err);
}

GConfValue*
gconf_engine_get_with_locale(GConfEngine* conf, const gchar* key,
                             const gchar* locale,
                             GError** err)
{
  return gconf_engine_get_full(conf, key, locale, TRUE,
                               NULL, NULL, err);
}

GConfValue*
gconf_engine_get_without_default(GConfEngine* conf, const gchar* key,
                                 GError** err)
{
  return gconf_engine_get_full(conf, key, NULL, FALSE, NULL, NULL, err);
}

GConfValue*
gconf_engine_get_default_from_schema (GConfEngine* conf,
                                      const gchar* key,
                                      GError** err)
{
  GConfValue* val;
  ConfigValue* cv;
  CORBA_Environment ev;
  ConfigDatabase db;
  gint tries = 0;

  g_return_val_if_fail(conf != NULL, NULL);
  g_return_val_if_fail(key != NULL, NULL);
  g_return_val_if_fail(err == NULL || *err == NULL, NULL);
  
  if (!gconf_key_check(key, err))
    return NULL;

  if (gconf_engine_is_local(conf))
    {
      gchar** locale_list;

      locale_list = gconf_split_locale(gconf_current_locale());
      
      val = gconf_sources_query_default_value(conf->local_sources,
                                              key,
                                              (const gchar**)locale_list,
                                              NULL,
                                              err);

      if (locale_list != NULL)
        g_strfreev(locale_list);
      
      return val;
    }

  g_assert(!gconf_engine_is_local(conf));
  
  CORBA_exception_init(&ev);

 RETRY:
  
  db = gconf_engine_get_database (conf, TRUE, err);

  if (db == CORBA_OBJECT_NIL)
    {
      g_return_val_if_fail(err == NULL || *err != NULL, NULL);

      return NULL;
    }

  cv = ConfigDatabase_lookup_default_value(db,
                                           (gchar*)key,
                                           (gchar*)gconf_current_locale(),
                                           &ev);
  
  if (gconf_server_broken(&ev))
    {
      if (tries < MAX_RETRIES)
        {
          ++tries;
          CORBA_exception_free(&ev);
          gconf_engine_detach (conf);
          goto RETRY;
        }
    }
  
  if (gconf_handle_corba_exception(&ev, err))
    {
      /* NOTE: don't free cv since we got an exception! */
      return NULL;
    }
  else
    {
      val = gconf_value_from_corba_value(cv);
      CORBA_free(cv);

      return val;
    }
}

gboolean
gconf_engine_set (GConfEngine* conf, const gchar* key,
                  const GConfValue* value, GError** err)
{
  ConfigValue* cv;
  CORBA_Environment ev;
  ConfigDatabase db;
  gint tries = 0;

  g_return_val_if_fail(conf != NULL, FALSE);
  g_return_val_if_fail(key != NULL, FALSE);
  g_return_val_if_fail(value != NULL, FALSE);
  g_return_val_if_fail(value->type != GCONF_VALUE_INVALID, FALSE);
  g_return_val_if_fail( (value->type != GCONF_VALUE_STRING) ||
                        (gconf_value_get_string(value) != NULL) , FALSE );
  g_return_val_if_fail( (value->type != GCONF_VALUE_LIST) ||
                        (gconf_value_get_list_type(value) != GCONF_VALUE_INVALID), FALSE);
  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);
  
  if (!gconf_key_check(key, err))
    return FALSE;

  if (gconf_engine_is_local(conf))
    {
      GError* error = NULL;
      
      gconf_sources_set_value(conf->local_sources, key, value, &error);

      if (error != NULL)
        {
          if (err)
            *err = error;
          else
            {
              g_error_free(error);
            }
          return FALSE;
        }
      
      return TRUE;
    }

  g_assert(!gconf_engine_is_local(conf));
  
  CORBA_exception_init(&ev);

 RETRY:
  
  db = gconf_engine_get_database (conf, TRUE, err);

  if (db == CORBA_OBJECT_NIL)
    {
      g_return_val_if_fail(err == NULL || *err != NULL, FALSE);

      return FALSE;
    }

  cv = corba_value_from_gconf_value(value);

  ConfigDatabase_set(db,
                     (gchar*)key, cv,
                     &ev);

  CORBA_free(cv);

  if (gconf_server_broken(&ev))
    {
      if (tries < MAX_RETRIES)
        {
          ++tries;
          CORBA_exception_free(&ev);
          gconf_engine_detach (conf);
          goto RETRY;
        }
    }
  
  if (gconf_handle_corba_exception(&ev, err))
    return FALSE;

  g_return_val_if_fail(*err == NULL, FALSE);
  
  return TRUE;
}

gboolean
gconf_engine_unset(GConfEngine* conf, const gchar* key, GError** err)
{
  CORBA_Environment ev;
  ConfigDatabase db;
  gint tries = 0;

  g_return_val_if_fail(conf != NULL, FALSE);
  g_return_val_if_fail(key != NULL, FALSE);
  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);
  
  if (!gconf_key_check(key, err))
    return FALSE;

  if (gconf_engine_is_local(conf))
    {
      GError* error = NULL;
      
      gconf_sources_unset_value(conf->local_sources, key, NULL, &error);

      if (error != NULL)
        {
          if (err)
            *err = error;
          else
            {
              g_error_free(error);
            }
          return FALSE;
        }
      
      return TRUE;
    }

  g_assert(!gconf_engine_is_local(conf));
  
  CORBA_exception_init(&ev);

 RETRY:
  
  db = gconf_engine_get_database (conf, TRUE, err);

  if (db == CORBA_OBJECT_NIL)
    {
      g_return_val_if_fail(err == NULL || *err != NULL, FALSE);

      return FALSE;
    }

  ConfigDatabase_unset(db,
                     (gchar*)key,
                     &ev);

  if (gconf_server_broken(&ev))
    {
      if (tries < MAX_RETRIES)
        {
          ++tries;
          CORBA_exception_free(&ev);
          gconf_engine_detach(conf);
          goto RETRY;
        }
    }
  
  if (gconf_handle_corba_exception(&ev, err))
    return FALSE;

  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);
  
  return TRUE;
}

gboolean
gconf_engine_associate_schema  (GConfEngine* conf, const gchar* key,
                                const gchar* schema_key, GError** err)
{
  CORBA_Environment ev;
  ConfigDatabase db;
  gint tries = 0;

  g_return_val_if_fail(conf != NULL, FALSE);
  g_return_val_if_fail(key != NULL, FALSE);
  g_return_val_if_fail(schema_key != NULL, FALSE);
  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);
  
  if (!gconf_key_check(key, err))
    return FALSE;

  if (!gconf_key_check(schema_key, err))
    return FALSE;

  if (gconf_engine_is_local(conf))
    {
      GError* error = NULL;
      
      gconf_sources_set_schema(conf->local_sources, key, schema_key, &error);

      if (error != NULL)
        {
          if (err)
            *err = error;
          else
            {
              g_error_free(error);
            }
          return FALSE;
        }
      
      return TRUE;
    }

  g_assert(!gconf_engine_is_local(conf));
  
  CORBA_exception_init(&ev);

 RETRY:
  
  db = gconf_engine_get_database(conf, TRUE, err);

  if (db == CORBA_OBJECT_NIL)
    {
      g_return_val_if_fail(err == NULL || *err != NULL, FALSE);

      return FALSE;
    }

  ConfigDatabase_set_schema(db,
                            (gchar*)key,
                            (gchar*)schema_key,
                            &ev);

  if (gconf_server_broken(&ev))
    {
      if (tries < MAX_RETRIES)
        {
          ++tries;
          CORBA_exception_free(&ev);
          gconf_engine_detach (conf);
          goto RETRY;
        }
    }
  
  if (gconf_handle_corba_exception(&ev, err))
    return FALSE;

  g_return_val_if_fail(*err == NULL, FALSE);
  
  return TRUE;
}


static void
qualify_entries (GSList *entries, const char *dir)
{
  GSList *tmp = entries;
  while (tmp != NULL)
    {
      GConfEntry *entry = tmp->data;
      gchar *full;

      full = gconf_concat_dir_and_key (dir, entry->key);

      g_free (entry->key);
      entry->key = full;

      tmp = g_slist_next (tmp);
    }
}

GSList*      
gconf_engine_all_entries(GConfEngine* conf, const gchar* dir, GError** err)
{
  GSList* pairs = NULL;
  ConfigDatabase_ValueList* values;
  ConfigDatabase_KeyList* keys;
  ConfigDatabase_IsDefaultList* is_defaults;
  ConfigDatabase_IsWritableList* is_writables;
  ConfigDatabase2_SchemaNameList *schema_names;
  CORBA_Environment ev;
  ConfigDatabase db;
  guint i;
  gint tries = 0;

  g_return_val_if_fail(conf != NULL, NULL);
  g_return_val_if_fail(dir != NULL, NULL);
  g_return_val_if_fail(err == NULL || *err == NULL, NULL);
  
  if (!gconf_key_check(dir, err))
    return NULL;


  if (gconf_engine_is_local(conf))
    {
      GError* error = NULL;
      gchar** locale_list;
      GSList* retval;
      
      locale_list = gconf_split_locale(gconf_current_locale());
      
      retval = gconf_sources_all_entries(conf->local_sources,
                                         dir,
                                         (const gchar**)locale_list,
                                         &error);

      if (locale_list)
        g_strfreev(locale_list);
      
      if (error != NULL)
        {
          if (err)
            *err = error;
          else
            {
              g_error_free(error);
            }

          g_assert(retval == NULL);
          
          return NULL;
        }

      qualify_entries (retval, dir);
      
      return retval;
    }

  g_assert(!gconf_engine_is_local(conf));
  
  CORBA_exception_init(&ev);
  
 RETRY:
  
  db = gconf_engine_get_database (conf, TRUE, err);

  if (db == CORBA_OBJECT_NIL)
    {
      g_return_val_if_fail(err == NULL || *err != NULL, NULL);

      return NULL;
    }

  schema_names = NULL;
  
  ConfigDatabase2_all_entries_with_schema_name (db,
                                                (gchar*)dir,
                                                (gchar*)gconf_current_locale(),
                                                &keys, &values, &schema_names,
                                                &is_defaults, &is_writables,
                                                &ev);
  
  if (ev._major == CORBA_SYSTEM_EXCEPTION &&
      CORBA_exception_id (&ev) &&
      strcmp (CORBA_exception_id (&ev), "IDL:CORBA/BAD_OPERATION:1.0") == 0)
    {
      CORBA_exception_free (&ev);
      CORBA_exception_init (&ev);
      
      ConfigDatabase_all_entries(db,
                                 (gchar*)dir,
                                 (gchar*)gconf_current_locale(),
                                 &keys, &values, &is_defaults, &is_writables,
                                 &ev);
    }

  if (gconf_server_broken(&ev))
    {
      if (tries < MAX_RETRIES)
        {
          ++tries;
          CORBA_exception_free(&ev);
          gconf_engine_detach (conf);
          goto RETRY;
        }
    }
  
  if (gconf_handle_corba_exception(&ev, err))
    return NULL;
  
  if (keys->_length != values->_length)
    {
      g_warning("Received unmatched key/value sequences in %s",
                G_GNUC_FUNCTION);
      return NULL;
    }

  i = 0;
  while (i < keys->_length)
    {
      GConfEntry* pair;

      pair = 
        gconf_entry_new_nocopy(gconf_concat_dir_and_key (dir, keys->_buffer[i]),
                               gconf_value_from_corba_value(&(values->_buffer[i])));

      /* note, there's an accesor function for setting this that we are
         cheating and not using */
      pair->is_default = is_defaults->_buffer[i];
      pair->is_writable = is_writables->_buffer[i];
      if (schema_names)
        {
          /* empty string means no schema name */
          if (*(schema_names->_buffer[i]) != '\0')
            pair->schema_name = g_strdup (schema_names->_buffer[i]);
        }
      
      pairs = g_slist_prepend(pairs, pair);
      
      ++i;
    }
  
  CORBA_free(keys);
  CORBA_free(values);
  CORBA_free(is_defaults);
  CORBA_free(is_writables);
  if (schema_names)
    CORBA_free (schema_names);
  
  return pairs;
}

GSList*      
gconf_engine_all_dirs(GConfEngine* conf, const gchar* dir, GError** err)
{
  GSList* subdirs = NULL;
  ConfigDatabase_KeyList* keys;
  CORBA_Environment ev;
  ConfigDatabase db;
  guint i;
  gint tries = 0;

  g_return_val_if_fail(conf != NULL, NULL);
  g_return_val_if_fail(dir != NULL, NULL);
  g_return_val_if_fail(err == NULL || *err == NULL, NULL);
  
  if (!gconf_key_check(dir, err))
    return NULL;

  if (gconf_engine_is_local(conf))
    {
      GError* error = NULL;
      GSList* retval;
      
      retval = gconf_sources_all_dirs(conf->local_sources,
                                      dir,
                                      &error);
      
      if (error != NULL)
        {
          if (err)
            *err = error;
          else
            {
              g_error_free(error);
            }

          g_assert(retval == NULL);
          
          return NULL;
        }

      qualify_entries (retval, dir);
      
      return retval;
    }

  g_assert(!gconf_engine_is_local(conf));
  
  CORBA_exception_init(&ev);
  
 RETRY:
  
  db = gconf_engine_get_database (conf, TRUE, err);

  if (db == CORBA_OBJECT_NIL)
    {
      g_return_val_if_fail(((err == NULL) || (*err && ((*err)->code == GCONF_ERROR_NO_SERVER))), NULL);

      return NULL;
    }
  
  ConfigDatabase_all_dirs(db,
                          (gchar*)dir, 
                          &keys,
                          &ev);

  if (gconf_server_broken(&ev))
    {
      if (tries < MAX_RETRIES)
        {
          ++tries;
          CORBA_exception_free(&ev);
          gconf_engine_detach (conf);
          goto RETRY;
        }
    }

  if (gconf_handle_corba_exception(&ev, err))
    return NULL;
  
  i = 0;
  while (i < keys->_length)
    {
      gchar* s;

      s = gconf_concat_dir_and_key (dir, keys->_buffer[i]);
      
      subdirs = g_slist_prepend(subdirs, s);
      
      ++i;
    }
  
  CORBA_free(keys);

  return subdirs;
}

/* annoyingly, this is REQUIRED for local sources */
void 
gconf_engine_suggest_sync(GConfEngine* conf, GError** err)
{
  CORBA_Environment ev;
  ConfigDatabase db;
  gint tries = 0;

  g_return_if_fail(conf != NULL);
  g_return_if_fail(err == NULL || *err == NULL);

  if (gconf_engine_is_local(conf))
    {
      GError* error = NULL;
      
      gconf_sources_sync_all(conf->local_sources,
                             &error);
      
      if (error != NULL)
        {
          if (err)
            *err = error;
          else
            {
              g_error_free(error);
            }
          return;
        }
      
      return;
    }

  g_assert(!gconf_engine_is_local(conf));
  
  CORBA_exception_init(&ev);

 RETRY:
  
  db = gconf_engine_get_database (conf, TRUE, err);

  if (db == CORBA_OBJECT_NIL)
    {
      g_return_if_fail(err == NULL || *err != NULL);

      return;
    }

  ConfigDatabase_sync(db, &ev);

  if (gconf_server_broken(&ev))
    {
      if (tries < MAX_RETRIES)
        {
          ++tries;
          CORBA_exception_free(&ev);
          gconf_engine_detach (conf);
          goto RETRY;
        }
    }
  
  if (gconf_handle_corba_exception(&ev, err))  
    ; /* nothing additional */
}

void 
gconf_clear_cache(GConfEngine* conf, GError** err)
{
  CORBA_Environment ev;
  ConfigDatabase db;
  gint tries = 0;

  g_return_if_fail(conf != NULL);
  g_return_if_fail(err == NULL || *err == NULL);

  if (gconf_engine_is_local(conf))
    {
      GError* error = NULL;
      
      gconf_sources_clear_cache(conf->local_sources);
      
      if (error != NULL)
        {
          if (err)
            *err = error;
          else
            {
              g_error_free(error);
            }
          return;
        }
      
      return;
    }

  g_assert(!gconf_engine_is_local(conf));
  
  CORBA_exception_init(&ev);

 RETRY:
  
  db = gconf_engine_get_database (conf, TRUE, err);

  if (db == CORBA_OBJECT_NIL)
    {
      g_return_if_fail(err == NULL || *err != NULL);

      return;
    }

  ConfigDatabase_clear_cache(db, &ev);

  if (gconf_server_broken(&ev))
    {
      if (tries < MAX_RETRIES)
        {
          ++tries;
          CORBA_exception_free(&ev);
          gconf_engine_detach (conf);
          goto RETRY;
        }
    }
  
  if (gconf_handle_corba_exception(&ev, err))  
    ; /* nothing additional */
}

void 
gconf_synchronous_sync(GConfEngine* conf, GError** err)
{
  CORBA_Environment ev;
  ConfigDatabase db;
  gint tries = 0;

  g_return_if_fail(conf != NULL);
  g_return_if_fail(err == NULL || *err == NULL);

  if (gconf_engine_is_local(conf))
    {
      GError* error = NULL;
      
      gconf_sources_sync_all(conf->local_sources, &error);
      
      if (error != NULL)
        {
          if (err)
            *err = error;
          else
            {
              g_error_free(error);
            }
          return;
        }
      
      return;
    }

  g_assert(!gconf_engine_is_local(conf));
  
  CORBA_exception_init(&ev);

 RETRY:
  
  db = gconf_engine_get_database (conf, TRUE, err);

  if (db == CORBA_OBJECT_NIL)
    {
      g_return_if_fail(err == NULL || *err != NULL);

      return;
    }

  ConfigDatabase_synchronous_sync(db, &ev);

  if (gconf_server_broken(&ev))
    {
      if (tries < MAX_RETRIES)
        {
          ++tries;
          CORBA_exception_free(&ev);
          gconf_engine_detach (conf);
          goto RETRY;
        }
    }
  
  if (gconf_handle_corba_exception(&ev, err))  
    ; /* nothing additional */
}

gboolean
gconf_engine_dir_exists(GConfEngine *conf, const gchar *dir, GError** err)
{
  CORBA_Environment ev;
  ConfigDatabase db;
  CORBA_boolean server_ret;
  gint tries = 0;

  g_return_val_if_fail(conf != NULL, FALSE);
  g_return_val_if_fail(dir != NULL, FALSE);
  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);
  
  if (!gconf_key_check(dir, err))
    return FALSE;
  
  if (gconf_engine_is_local(conf))
    {
      return gconf_sources_dir_exists(conf->local_sources,
                                      dir,
                                      err);
    }

  g_assert(!gconf_engine_is_local(conf));
  
  CORBA_exception_init(&ev);
  
 RETRY:
  
  db = gconf_engine_get_database(conf, TRUE, err);
  
  if (db == CORBA_OBJECT_NIL)
    {
      g_return_val_if_fail(err == NULL || *err != NULL, FALSE);

      return FALSE;
    }
  
  server_ret = ConfigDatabase_dir_exists(db,
                                         (gchar*)dir,
                                         &ev);
  
  if (gconf_server_broken(&ev))
    {
      if (tries < MAX_RETRIES)
        {
          ++tries;
          CORBA_exception_free(&ev);
          gconf_engine_detach (conf);
          goto RETRY;
        }
    }
  
  if (gconf_handle_corba_exception(&ev, err))  
    ; /* nothing */

  return (server_ret == CORBA_TRUE);
}

void
gconf_engine_remove_dir (GConfEngine* conf,
                         const gchar* dir,
                         GError** err)
{
  CORBA_Environment ev;
  ConfigDatabase db;
  gint tries = 0;

  g_return_if_fail(conf != NULL);
  g_return_if_fail(dir != NULL);
  g_return_if_fail(err == NULL || *err == NULL);
  
  if (!gconf_key_check(dir, err))
    return;

  if (gconf_engine_is_local(conf))
    {
      gconf_sources_remove_dir(conf->local_sources, dir, err);
      return;
    }

  CORBA_exception_init(&ev);
  
 RETRY:
  
  db = gconf_engine_get_database (conf, TRUE, err);

  if (db == CORBA_OBJECT_NIL)
    {
      g_return_if_fail(err == NULL || *err != NULL);
      return;
    }
  
  ConfigDatabase_remove_dir(db, (gchar*)dir, &ev);

  if (gconf_server_broken(&ev))
    {
      if (tries < MAX_RETRIES)
        {
          ++tries;
          CORBA_exception_free(&ev);
          gconf_engine_detach (conf);
          goto RETRY;
        }
    }
  gconf_handle_corba_exception(&ev, err);
  
  return;
}

gboolean
gconf_engine_key_is_writable  (GConfEngine *conf,
                               const gchar *key,
                               GError     **err)
{
  gboolean is_writable = TRUE;
  GConfValue *val;

  /* FIXME implement IDL to allow getting only writability
   * (not that urgent since GConfClient caches this crap
   * anyway)
   */
  
  val = gconf_engine_get_full(conf, key, NULL, TRUE,
                              NULL, &is_writable, err);

  gconf_value_free (val);
  
  return is_writable;
}

/*
 * Connection maintenance
 */

static GConfCnxn* 
gconf_cnxn_new(GConfEngine* conf,
               const gchar* namespace_section,
               CORBA_unsigned_long server_id,
               GConfNotifyFunc func,
               gpointer user_data)
{
  GConfCnxn* cnxn;
  static guint next_id = 1;
  
  cnxn = g_new0(GConfCnxn, 1);

  cnxn->namespace_section = g_strdup(namespace_section);
  cnxn->conf = conf;
  cnxn->server_id = server_id;
  cnxn->client_id = next_id;
  cnxn->func = func;
  cnxn->user_data = user_data;

  ++next_id;

  return cnxn;
}

static void      
gconf_cnxn_destroy(GConfCnxn* cnxn)
{
  g_free(cnxn->namespace_section);
  g_free(cnxn);
}

static void       
gconf_cnxn_notify(GConfCnxn* cnxn,
                  GConfEntry *entry)
{
  (*cnxn->func)(cnxn->conf, cnxn->client_id,
                entry,
                cnxn->user_data);
}

/*
 *  CORBA glue
 */

static ConfigServer   server = CORBA_OBJECT_NIL;

/* errors in here should be GCONF_ERROR_NO_SERVER */
static ConfigServer
try_to_contact_server (gboolean start_if_not_found,
                       GError **err)
{
  CORBA_Environment ev;
  
  /* Try to launch server */      
  server = gconf_activate_server (start_if_not_found,
                                  err);
    
  /* Try to ping server, by adding ourselves as a client */
  CORBA_exception_init (&ev);   

  if (!CORBA_Object_is_nil (server, &ev))
    {
      ConfigServer_add_client (server,
                               gconf_get_config_listener (),
                               &ev);
      
      if (ev._major != CORBA_NO_EXCEPTION)
	{
	  server = CORBA_OBJECT_NIL;
          g_set_error (err,
                       GCONF_ERROR,
                       GCONF_ERROR_NO_SERVER,
                       _("Adding client to server's list failed, CORBA error: %s"),
                       CORBA_exception_id (&ev));

          CORBA_exception_free(&ev);
	}
    }

#ifdef GCONF_ENABLE_DEBUG      
  if (server == CORBA_OBJECT_NIL && start_if_not_found)
    g_return_val_if_fail (err == NULL || *err != NULL, server);
#endif
  
  return server;
}

/* All errors set in here should be GCONF_ERROR_NO_SERVER; should
   only set errors if start_if_not_found is TRUE */
static ConfigServer
gconf_get_config_server(gboolean start_if_not_found, GError** err)
{
  g_return_val_if_fail(err == NULL || *err == NULL, server);
  
  if (server != CORBA_OBJECT_NIL)
    return server;

  server = try_to_contact_server(start_if_not_found, err);
  
  return server; /* return what we have, NIL or not */
}

static void
gconf_detach_config_server(void)
{  
  if (server != CORBA_OBJECT_NIL)
    {
      CORBA_Environment ev;
      
      CORBA_exception_init(&ev);

      CORBA_Object_release(server, &ev);

      if (ev._major != CORBA_NO_EXCEPTION)
        {
          g_warning("Exception releasing gconfd server object: %s",
                    CORBA_exception_id(&ev));
          CORBA_exception_free(&ev);
        }

      server = CORBA_OBJECT_NIL;
    }
}

ConfigListener listener = CORBA_OBJECT_NIL;
static void notify                  (PortableServer_Servant     servant,
                                     ConfigDatabase             db,
                                     CORBA_unsigned_long        cnxn,
                                     const CORBA_char          *key,
                                     const ConfigValue         *value,
                                     CORBA_boolean              is_default,
                                     CORBA_boolean              is_writable,
                                     CORBA_Environment         *ev);
static void ping                    (PortableServer_Servant     _servant,
                                     CORBA_Environment         *ev);
static void update_listener         (PortableServer_Servant     _servant,
                                     ConfigDatabase             db,
                                     const CORBA_char          *address,
                                     const CORBA_unsigned_long  old_cnxn,
                                     const CORBA_char          *key,
                                     const CORBA_unsigned_long  new_cnxn,
                                     CORBA_Environment         *ev);
static void invalidate_cached_values(PortableServer_Servant     _servant,
                                     ConfigDatabase             database,
                                     const ConfigListener_KeyList *keys,
                                     CORBA_Environment         *ev);
static void drop_all_caches         (PortableServer_Servant     _servant,
                                     CORBA_Environment         *ev);



static PortableServer_ServantBase__epv base_epv = {
  NULL,
  NULL,
  NULL
};

static POA_ConfigListener__epv listener_epv = {
  NULL,
  notify,
  ping,
  update_listener,
  invalidate_cached_values,
  drop_all_caches
};

static POA_ConfigListener__vepv poa_listener_vepv = { &base_epv, &listener_epv };
static POA_ConfigListener poa_listener_servant = { NULL, &poa_listener_vepv };

static void 
notify(PortableServer_Servant servant,
       ConfigDatabase db,
       CORBA_unsigned_long server_id,
       const CORBA_char* key,
       const ConfigValue* value,
       CORBA_boolean is_default,
       CORBA_boolean is_writable,
       CORBA_Environment *ev)
{
  GConfCnxn* cnxn;
  GConfValue* gvalue;
  GConfEngine* conf;
  GConfEntry* entry;
  
  conf = lookup_engine_by_database (db);

  if (conf == NULL)
    {
#ifdef GCONF_ENABLE_DEBUG
      g_warning ("Client received notify for unknown database object");
#endif
      return;
    }
  
  cnxn = ctable_lookup_by_server_id(conf->ctable, server_id);
  
  if (cnxn == NULL)
    {
#ifdef GCONF_ENABLE_DEBUG
      g_warning("Client received notify for unknown connection ID %u",
                (guint)server_id);
#endif
      return;
    }

  gvalue = gconf_value_from_corba_value(value);

  entry = gconf_entry_new_nocopy (g_strdup (key),
                                  gvalue);
  entry->is_default = is_default;
  entry->is_writable = is_writable;
  
  gconf_cnxn_notify(cnxn, entry);

  gconf_entry_free (entry);
}

static void
ping (PortableServer_Servant _servant, CORBA_Environment * ev)
{
  /* This one is easy :-) */
  
  return;
}

static void
update_listener (PortableServer_Servant _servant,
                 ConfigDatabase             db,
                 const CORBA_char          *address,
                 const CORBA_unsigned_long  old_cnxn_id,
                 const CORBA_char          *key,
                 const CORBA_unsigned_long  new_cnxn_id,
                 CORBA_Environment         *ev_ignored)
{
  GConfCnxn* cnxn;
  GConfEngine* conf;
  CORBA_Environment ev;
  
  conf = lookup_engine_by_database (db);

  /* See if we have an old engine with a now-invalid object
     reference, and update its reference. */
  if (conf == NULL)
    {
      CORBA_exception_init (&ev);
      
      if (strcmp (address, "def") == 0)
        conf = default_engine;
      else
        conf = lookup_engine (address);

      if (conf)
        gconf_engine_set_database (conf,
                                   CORBA_Object_duplicate (db, &ev));
    }
  
  if (conf == NULL)
    {
#ifdef GCONF_ENABLE_DEBUG
      g_warning("Client received listener update for unknown database "
                "(this is not a big deal, this warning only appears if GConf is compiled with debugging)");
#endif
      return;
    }
  
  cnxn = ctable_lookup_by_server_id (conf->ctable, old_cnxn_id);
  
  if (cnxn == NULL)
    {
#ifdef GCONF_ENABLE_DEBUG
      g_warning("Client received listener update for unknown listener ID %u "
                "(this is not a big deal, this warning only appears if GConf is compiled with debugging)",
                (guint)old_cnxn_id);
#endif
      return;
    }
  
  ctable_reinstall (conf->ctable, cnxn, old_cnxn_id, new_cnxn_id);
}

static void
invalidate_cached_values (PortableServer_Servant     _servant,
                          ConfigDatabase             database,
                          const ConfigListener_KeyList *keys,
                          CORBA_Environment         *ev)
{
#if 0
  g_warning ("FIXME process %d received request to invalidate some cached GConf values from the server, but right now we don't know how to do that (not implemented).", (int) getpid());
#endif
}

static void
drop_all_caches (PortableServer_Servant     _servant,
                 CORBA_Environment         *ev)
{
#if 0
  g_warning ("FIXME process %d received request to invalidate all cached GConf values from the server, but right now we don't know how to do that (not implemented).", (int) getpid());
#endif
}

static ConfigListener 
gconf_get_config_listener(void)
{
  static ConfigListener listener = CORBA_OBJECT_NIL;
  
  if (listener == CORBA_OBJECT_NIL)
    {
      CORBA_Environment ev;
      PortableServer_ObjectId* objid;
      PortableServer_POA poa;

      CORBA_exception_init (&ev);
      POA_ConfigListener__init (&poa_listener_servant, &ev);
      
      g_assert (ev._major == CORBA_NO_EXCEPTION);

      poa =
        (PortableServer_POA) CORBA_ORB_resolve_initial_references (gconf_orb_get (),
                                                                   "RootPOA", &ev);

      g_assert (ev._major == CORBA_NO_EXCEPTION);

      PortableServer_POAManager_activate(PortableServer_POA__get_the_POAManager(poa, &ev), &ev);

      g_assert (ev._major == CORBA_NO_EXCEPTION);

      objid = PortableServer_POA_activate_object (poa, &poa_listener_servant, &ev);

      g_assert (ev._major == CORBA_NO_EXCEPTION);
      
      listener = PortableServer_POA_servant_to_reference(poa,
                                                         &poa_listener_servant,
                                                         &ev);

      g_assert (listener != CORBA_OBJECT_NIL);
      g_assert (ev._major == CORBA_NO_EXCEPTION);
    }
  
  return listener;
}
     
void
gconf_preinit (gpointer app, gpointer mod_info)
{
  /* Deprecated */
}

void
gconf_postinit (gpointer app, gpointer mod_info)
{
  /* Deprecated */
}

/* All deprecated */
const char gconf_version[] = VERSION;

struct poptOption gconf_options[] = {
  {NULL}
};

/* Also deprecated */
gboolean     
gconf_init (int argc, char **argv, GError** err)
{
  
  return TRUE;
}

gboolean
gconf_is_initialized (void)
{
  return TRUE;
}

/* 
 * Ampersand and <> are not allowed due to the XML backend; shell
 * special characters aren't allowed; others are just in case we need
 * some magic characters someday.  hyphen, underscore, period, colon
 * are allowed as separators. % disallowed to avoid printf confusion.
 */

/* Key/dir validity is exactly the same, except that '/' must be a dir, 
   but we are sort of ignoring that for now. */

static const gchar invalid_chars[] = " \t\r\n\"$&<>,+=#!()'|{}[]?~`;%\\";

gboolean     
gconf_valid_key      (const gchar* key, gchar** why_invalid)
{
  const gchar* s = key;
  gboolean just_saw_slash = FALSE;

  /* Key must start with the root */
  if (*key != '/')
    {
      if (why_invalid != NULL)
        *why_invalid = g_strdup(_("Must begin with a slash (/)"));
      return FALSE;
    }
  
  /* Root key is a valid dir */
  if (*key == '/' && key[1] == '\0')
    return TRUE;

  while (*s)
    {
      if (just_saw_slash)
        {
          /* Can't have two slashes in a row, since it would mean
           * an empty spot.
           * Can't have a period right after a slash,
           * because it would be a pain for filesystem-based backends.
           */
          if (*s == '/' || *s == '.')
            {
              if (why_invalid != NULL)
                {
                  if (*s == '/')
                    *why_invalid = g_strdup(_("Can't have two slashes (/) in a row"));
                  else
                    *why_invalid = g_strdup(_("Can't have a period (.) right after a slash (/)"));
                }
              return FALSE;
            }
        }

      if (*s == '/')
        {
          just_saw_slash = TRUE;
        }
      else
        {
          const gchar* inv = invalid_chars;

          just_saw_slash = FALSE;

          while (*inv)
            {
              if (*inv == *s)
                {
                  if (why_invalid != NULL)
                    *why_invalid = g_strdup_printf(_("`%c' is an invalid character in key/directory names"), *s);
                  return FALSE;
                }
              ++inv;
            }
        }

      ++s;
    }

  /* Can't end with slash */
  if (just_saw_slash)
    {
      if (why_invalid != NULL)
        *why_invalid = g_strdup(_("Key/directory may not end with a slash (/)"));
      return FALSE;
    }
  else
    return TRUE;
}

gboolean
gconf_key_is_below   (const gchar* above, const gchar* below)
{
  int len = strlen(above);
  if (strncmp(below, above, len) == 0)
    {
      /* only if this is a complete key component,
       * so that /foo is not above /foofoo/bar */
      if (below[len] == '\0' || below[len] == '/')
        return TRUE;
      else
	return FALSE;
    }
  else
    return FALSE;
}

gchar*
gconf_unique_key (void)
{
  /* This function is hardly cryptographically random but should be
     "good enough" */
  
  static guint serial = 0;
  gchar* key;
  guint t, ut, p, u, r;
  struct timeval tv;
  
  gettimeofday(&tv, NULL);
  
  t = tv.tv_sec;
  ut = tv.tv_usec;

  p = getpid();
  
  u = getuid();

  /* don't bother to seed; if it's based on the time or any other
     changing info we can get, we may as well just use that changing
     info. since we don't seed we'll at least get a different number
     on every call to this function in the same executable. */
  r = rand();
  
  /* The letters may increase uniqueness by preventing "melds"
     i.e. 01t01k01 and 0101t0k1 are not the same */
  key = g_strdup_printf("%ut%uut%uu%up%ur%uk%u",
                        /* Duplicate keys must be generated
                           by two different program instances */
                        serial,
                        /* Duplicate keys must be generated
                           in the same microsecond */
                        t,
                        ut,
                        /* Duplicate keys must be generated by
                           the same user */
                        u,
                        /* Duplicate keys must be generated by
                           two programs that got the same PID */
                        p,
                        /* Duplicate keys must be generated with the
                           same random seed and the same index into
                           the series of pseudorandom values */
                        r,
                        /* Duplicate keys must result from running
                           this function at the same stack location */
                        GPOINTER_TO_UINT(&key));

  ++serial;
  
  return key;
}

/*
 * Table of connections 
 */ 

static gint
corba_unsigned_long_equal (gconstpointer v1,
                           gconstpointer v2)
{
  return *((const CORBA_unsigned_long*) v1) == *((const CORBA_unsigned_long*) v2);
}

static guint
corba_unsigned_long_hash (gconstpointer v)
{
  /* for our purposes we can just assume 32 bits are significant */
  return (guint)(*(const CORBA_unsigned_long*) v);
}

static CnxnTable* 
ctable_new(void)
{
  CnxnTable* ct;

  ct = g_new(CnxnTable, 1);

  ct->server_ids = g_hash_table_new (corba_unsigned_long_hash,
                                     corba_unsigned_long_equal);  
  ct->client_ids = g_hash_table_new (g_int_hash, g_int_equal);
  
  return ct;
}

static void
ctable_destroy(CnxnTable* ct)
{
  g_hash_table_destroy (ct->server_ids);
  g_hash_table_destroy (ct->client_ids);
  g_free(ct);
}

static void       
ctable_insert(CnxnTable* ct, GConfCnxn* cnxn)
{
  g_hash_table_insert (ct->server_ids, &cnxn->server_id, cnxn);
  g_hash_table_insert (ct->client_ids, &cnxn->client_id, cnxn);
}

static void       
ctable_remove(CnxnTable* ct, GConfCnxn* cnxn)
{
  g_hash_table_remove (ct->server_ids, &cnxn->server_id);
  g_hash_table_remove (ct->client_ids, &cnxn->client_id);
}

static void       
ctable_remove_by_client_id(CnxnTable* ct, guint client_id)
{
  GConfCnxn* cnxn;

  cnxn = ctable_lookup_by_client_id (ct, client_id);

  g_return_if_fail (cnxn != NULL);

  ctable_remove (ct, cnxn);
}

struct RemoveData {
  GSList* removed;
  GConfEngine* conf;
  gboolean save_removed;
};

static gboolean
remove_by_conf(gpointer key, gpointer value, gpointer user_data)
{
  struct RemoveData* rd = user_data;
  GConfCnxn* cnxn = value;
  
  if (cnxn->conf == rd->conf)
    {
      if (rd->save_removed)
        rd->removed = g_slist_prepend(rd->removed, cnxn);

      return TRUE;  /* remove this one */
    }
  else 
    return FALSE; /* or not */
}

/* FIXME this no longer makes any sense, because a CnxnTable
   belongs to a GConfEngine and all entries have the same
   GConfEngine.
*/

/* We return a list of the removed GConfCnxn */
static GSList*      
ctable_remove_by_conf(CnxnTable* ct, GConfEngine* conf)
{
  guint client_ids_removed;
  guint server_ids_removed;
  struct RemoveData rd;

  rd.removed = NULL;
  rd.conf = conf;
  rd.save_removed = TRUE;
  
  client_ids_removed = g_hash_table_foreach_remove (ct->server_ids,
                                                    remove_by_conf,
                                                    &rd);

  rd.save_removed = FALSE;

  server_ids_removed = g_hash_table_foreach_remove(ct->client_ids,
                                                   remove_by_conf,
                                                   &rd);

  g_assert(client_ids_removed == server_ids_removed);
  g_assert(client_ids_removed == g_slist_length(rd.removed));

  return rd.removed;
}

static GConfCnxn* 
ctable_lookup_by_client_id(CnxnTable* ct, guint client_id)
{
  return g_hash_table_lookup(ct->client_ids, &client_id);
}

static GConfCnxn* 
ctable_lookup_by_server_id(CnxnTable* ct, CORBA_unsigned_long server_id)
{
  return g_hash_table_lookup (ct->server_ids, &server_id);
}

static void
ctable_reinstall (CnxnTable* ct,
                  GConfCnxn *cnxn,
                  guint old_server_id,
                  guint new_server_id)
{
  g_return_if_fail (cnxn->server_id == old_server_id);

  g_hash_table_remove (ct->server_ids, &old_server_id);
  
  cnxn->server_id = new_server_id;

  g_hash_table_insert (ct->server_ids, &cnxn->server_id, cnxn);
}

/*
 * Daemon control
 */

void          
gconf_shutdown_daemon(GError** err)
{
  CORBA_Environment ev;
  ConfigServer cs;

  cs = gconf_get_config_server(FALSE, err); /* Don't want to spawn it if it's already down */

  if (cs == CORBA_OBJECT_NIL)
    {

      return;
    }

  CORBA_exception_init(&ev);

  ConfigServer_shutdown(cs, &ev);

  if (ev._major != CORBA_NO_EXCEPTION)
    {
      if (err)
        *err = gconf_error_new(GCONF_ERROR_FAILED, _("Failure shutting down config server: %s"),
                               CORBA_exception_id(&ev));

      CORBA_exception_free(&ev);
    }
}

gboolean
gconf_ping_daemon(void)
{
  ConfigServer cs;
  
  cs = gconf_get_config_server(FALSE, NULL); /* ignore error, since whole point is to see if server is reachable */

  if (cs == CORBA_OBJECT_NIL)
    return FALSE;
  else
    return TRUE;
}

gboolean
gconf_spawn_daemon(GError** err)
{
  ConfigServer cs;

  cs = gconf_get_config_server(TRUE, err);

  if (cs == CORBA_OBJECT_NIL)
    {
      g_return_val_if_fail(err == NULL || *err != NULL, FALSE);
      return FALSE; /* Failed to spawn, error should be set */
    }
  else
    return TRUE;
}

/*
 * Sugar functions 
 */

gdouble      
gconf_engine_get_float (GConfEngine* conf, const gchar* key,
                 GError** err)
{
  GConfValue* val;
  static const gdouble deflt = 0.0;
  
  g_return_val_if_fail(conf != NULL, 0.0);
  g_return_val_if_fail(key != NULL, 0.0);
  
  val = gconf_engine_get (conf, key, err);

  if (val == NULL)
    return deflt;
  else
    {
      gdouble retval;
      
      if (val->type != GCONF_VALUE_FLOAT)
        {
          if (err)
            *err = gconf_error_new(GCONF_ERROR_TYPE_MISMATCH, _("Expected float, got %s"),
                                    gconf_value_type_to_string(val->type));
          gconf_value_free(val);
          return deflt;
        }

      retval = gconf_value_get_float(val);

      gconf_value_free(val);

      return retval;
    }
}

gint         
gconf_engine_get_int   (GConfEngine* conf, const gchar* key,
                 GError** err)
{
  GConfValue* val;
  static const gint deflt = 0;
  
  g_return_val_if_fail(conf != NULL, 0);
  g_return_val_if_fail(key != NULL, 0);
  
  val = gconf_engine_get (conf, key, err);

  if (val == NULL)
    return deflt;
  else
    {
      gint retval;

      if (val->type != GCONF_VALUE_INT)
        {
          if (err)
            *err = gconf_error_new(GCONF_ERROR_TYPE_MISMATCH, _("Expected int, got %s"),
                                    gconf_value_type_to_string(val->type));
          gconf_value_free(val);
          return deflt;
        }

      retval = gconf_value_get_int(val);

      gconf_value_free(val);

      return retval;
    }
}

gchar*       
gconf_engine_get_string(GConfEngine* conf, const gchar* key,
                 GError** err)
{
  GConfValue* val;
  static const gchar* deflt = NULL;
  
  g_return_val_if_fail(conf != NULL, NULL);
  g_return_val_if_fail(key != NULL, NULL);
  
  val = gconf_engine_get (conf, key, err);

  if (val == NULL)
    return deflt ? g_strdup(deflt) : NULL;
  else
    {
      gchar* retval;

      if (val->type != GCONF_VALUE_STRING)
        {
          if (err)
            *err = gconf_error_new(GCONF_ERROR_TYPE_MISMATCH, _("Expected string, got %s"),
                                    gconf_value_type_to_string(val->type));
          gconf_value_free(val);
          return deflt ? g_strdup(deflt) : NULL;
        }

      retval = val->d.string_data;
      /* This is a cheat; don't copy */
      val->d.string_data = NULL; /* don't delete the string */

      gconf_value_free(val);

      return retval;
    }
}

gboolean     
gconf_engine_get_bool  (GConfEngine* conf, const gchar* key,
                        GError** err)
{
  GConfValue* val;
  static const gboolean deflt = FALSE;
  
  g_return_val_if_fail(conf != NULL, FALSE);
  g_return_val_if_fail(key != NULL, FALSE);
  
  val = gconf_engine_get (conf, key, err);

  if (val == NULL)
    return deflt;
  else
    {
      gboolean retval;

      if (val->type != GCONF_VALUE_BOOL)
        {
          if (err)
            *err = gconf_error_new(GCONF_ERROR_TYPE_MISMATCH, _("Expected bool, got %s"),
                                    gconf_value_type_to_string(val->type));
          gconf_value_free(val);
          return deflt;
        }

      retval = gconf_value_get_bool(val);

      gconf_value_free(val);

      return retval;
    }
}

GConfSchema* 
gconf_engine_get_schema  (GConfEngine* conf, const gchar* key, GError** err)
{
  GConfValue* val;

  g_return_val_if_fail(conf != NULL, NULL);
  g_return_val_if_fail(key != NULL, NULL);
  
  val = gconf_engine_get_with_locale(conf, key, gconf_current_locale(), err);

  if (val == NULL)
    return NULL;
  else
    {
      GConfSchema* retval;

      if (val->type != GCONF_VALUE_SCHEMA)
        {
          if (err)
            *err = gconf_error_new(GCONF_ERROR_TYPE_MISMATCH, _("Expected schema, got %s"),
                                    gconf_value_type_to_string(val->type));
          gconf_value_free(val);
          return NULL;
        }

      retval = gconf_value_get_schema(val);

      /* This is a cheat; don't copy */
      val->d.schema_data = NULL; /* don't delete the schema */

      gconf_value_free(val);

      return retval;
    }
}

GSList*
gconf_engine_get_list    (GConfEngine* conf, const gchar* key,
                          GConfValueType list_type, GError** err)
{
  GConfValue* val;

  g_return_val_if_fail(conf != NULL, NULL);
  g_return_val_if_fail(key != NULL, NULL);
  g_return_val_if_fail(list_type != GCONF_VALUE_INVALID, NULL);
  g_return_val_if_fail(list_type != GCONF_VALUE_LIST, NULL);
  g_return_val_if_fail(list_type != GCONF_VALUE_PAIR, NULL);
  g_return_val_if_fail(err == NULL || *err == NULL, NULL);
  
  val = gconf_engine_get_with_locale(conf, key, gconf_current_locale(), err);

  if (val == NULL)
    return NULL;
  else
    {
      /* This type-checks the value */
      return gconf_value_list_to_primitive_list_destructive(val, list_type, err);
    }
}

gboolean
gconf_engine_get_pair    (GConfEngine* conf, const gchar* key,
                   GConfValueType car_type, GConfValueType cdr_type,
                   gpointer car_retloc, gpointer cdr_retloc,
                   GError** err)
{
  GConfValue* val;
  GError* error = NULL;
  
  g_return_val_if_fail(conf != NULL, FALSE);
  g_return_val_if_fail(key != NULL, FALSE);
  g_return_val_if_fail(car_type != GCONF_VALUE_INVALID, FALSE);
  g_return_val_if_fail(car_type != GCONF_VALUE_LIST, FALSE);
  g_return_val_if_fail(car_type != GCONF_VALUE_PAIR, FALSE);
  g_return_val_if_fail(cdr_type != GCONF_VALUE_INVALID, FALSE);
  g_return_val_if_fail(cdr_type != GCONF_VALUE_LIST, FALSE);
  g_return_val_if_fail(cdr_type != GCONF_VALUE_PAIR, FALSE);
  g_return_val_if_fail(car_retloc != NULL, FALSE);
  g_return_val_if_fail(cdr_retloc != NULL, FALSE);
  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);  
  
  val = gconf_engine_get_with_locale(conf, key, gconf_current_locale(), &error);

  if (error != NULL)
    {
      g_assert(val == NULL);
      
      if (err)
        *err = error;
      else
        g_error_free(error);

      return FALSE;
    }
  
  if (val == NULL)
    {
      return TRUE;
    }
  else
    {
      /* Destroys val */
      return gconf_value_pair_to_primitive_pair_destructive(val,
                                                            car_type, cdr_type,
                                                            car_retloc, cdr_retloc,
                                                            err);
    }
}

/*
 * Setters
 */

static gboolean
error_checked_set(GConfEngine* conf, const gchar* key,
                  GConfValue* gval, GError** err)
{
  GError* my_err = NULL;
  
  gconf_engine_set (conf, key, gval, &my_err);

  gconf_value_free(gval);
  
  if (my_err != NULL)
    {
      if (err)
        *err = my_err;
      else
        g_error_free(my_err);
      return FALSE;
    }
  else
    return TRUE;
}

gboolean
gconf_engine_set_float   (GConfEngine* conf, const gchar* key,
                   gdouble val, GError** err)
{
  GConfValue* gval;

  g_return_val_if_fail(conf != NULL, FALSE);
  g_return_val_if_fail(key != NULL, FALSE);
  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);
  
  gval = gconf_value_new(GCONF_VALUE_FLOAT);

  gconf_value_set_float(gval, val);

  return error_checked_set(conf, key, gval, err);
}

gboolean
gconf_engine_set_int     (GConfEngine* conf, const gchar* key,
                   gint val, GError** err)
{
  GConfValue* gval;

  g_return_val_if_fail(conf != NULL, FALSE);
  g_return_val_if_fail(key != NULL, FALSE);
  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);
  
  gval = gconf_value_new(GCONF_VALUE_INT);

  gconf_value_set_int(gval, val);

  return error_checked_set(conf, key, gval, err);
}

gboolean
gconf_engine_set_string  (GConfEngine* conf, const gchar* key,
                    const gchar* val, GError** err)
{
  GConfValue* gval;

  g_return_val_if_fail(val != NULL, FALSE);
  g_return_val_if_fail(conf != NULL, FALSE);
  g_return_val_if_fail(key != NULL, FALSE);
  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);
  
  gval = gconf_value_new(GCONF_VALUE_STRING);

  gconf_value_set_string(gval, val);

  return error_checked_set(conf, key, gval, err);
}

gboolean
gconf_engine_set_bool    (GConfEngine* conf, const gchar* key,
                   gboolean val, GError** err)
{
  GConfValue* gval;

  g_return_val_if_fail(conf != NULL, FALSE);
  g_return_val_if_fail(key != NULL, FALSE);
  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);
  
  gval = gconf_value_new(GCONF_VALUE_BOOL);

  gconf_value_set_bool(gval, !!val); /* canonicalize the bool */

  return error_checked_set(conf, key, gval, err);
}

gboolean
gconf_engine_set_schema  (GConfEngine* conf, const gchar* key,
                    const GConfSchema* val, GError** err)
{
  GConfValue* gval;

  g_return_val_if_fail(conf != NULL, FALSE);
  g_return_val_if_fail(key != NULL, FALSE);
  g_return_val_if_fail(val != NULL, FALSE);
  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);
  
  gval = gconf_value_new(GCONF_VALUE_SCHEMA);

  gconf_value_set_schema(gval, val);

  return error_checked_set(conf, key, gval, err);
}

gboolean
gconf_engine_set_list    (GConfEngine* conf, const gchar* key,
                   GConfValueType list_type,
                   GSList* list,
                   GError** err)
{
  GConfValue* value_list;
  
  g_return_val_if_fail(conf != NULL, FALSE);
  g_return_val_if_fail(key != NULL, FALSE);
  g_return_val_if_fail(list_type != GCONF_VALUE_INVALID, FALSE);
  g_return_val_if_fail(list_type != GCONF_VALUE_LIST, FALSE);
  g_return_val_if_fail(list_type != GCONF_VALUE_PAIR, FALSE);
  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);

  value_list = gconf_value_list_from_primitive_list(list_type, list);
  
  /* destroys the value_list */
  
  return error_checked_set(conf, key, value_list, err);
}

gboolean
gconf_engine_set_pair    (GConfEngine* conf, const gchar* key,
                   GConfValueType car_type, GConfValueType cdr_type,
                   gconstpointer address_of_car,
                   gconstpointer address_of_cdr,
                   GError** err)
{
  GConfValue* pair;
  
  g_return_val_if_fail(conf != NULL, FALSE);
  g_return_val_if_fail(key != NULL, FALSE);
  g_return_val_if_fail(car_type != GCONF_VALUE_INVALID, FALSE);
  g_return_val_if_fail(car_type != GCONF_VALUE_LIST, FALSE);
  g_return_val_if_fail(car_type != GCONF_VALUE_PAIR, FALSE);
  g_return_val_if_fail(cdr_type != GCONF_VALUE_INVALID, FALSE);
  g_return_val_if_fail(cdr_type != GCONF_VALUE_LIST, FALSE);
  g_return_val_if_fail(cdr_type != GCONF_VALUE_PAIR, FALSE);
  g_return_val_if_fail(address_of_car != NULL, FALSE);
  g_return_val_if_fail(address_of_cdr != NULL, FALSE);
  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);
  

  pair = gconf_value_pair_from_primitive_pair(car_type, cdr_type,
                                              address_of_car, address_of_cdr);
  
  return error_checked_set(conf, key, pair, err);
}

/* CORBA Util */

/* Set GConfError from an exception, free exception, etc. */

static GConfError
corba_errno_to_gconf_errno(ConfigErrorType corba_err)
{
  switch (corba_err)
    {
    case ConfigFailed:
      return GCONF_ERROR_FAILED;
      break;
    case ConfigNoPermission:
      return GCONF_ERROR_NO_PERMISSION;
      break;
    case ConfigBadAddress:
      return GCONF_ERROR_BAD_ADDRESS;
      break;
    case ConfigBadKey:
      return GCONF_ERROR_BAD_KEY;
      break;
    case ConfigParseError:
      return GCONF_ERROR_PARSE_ERROR;
      break;
    case ConfigCorrupt:
      return GCONF_ERROR_CORRUPT;
      break;
    case ConfigTypeMismatch:
      return GCONF_ERROR_TYPE_MISMATCH;
      break;
    case ConfigIsDir:
      return GCONF_ERROR_IS_DIR;
      break;
    case ConfigIsKey:
      return GCONF_ERROR_IS_KEY;
      break;
    case ConfigOverridden:
      return GCONF_ERROR_OVERRIDDEN;
      break;
    case ConfigLockFailed:
      return GCONF_ERROR_LOCK_FAILED;
      break;
    case ConfigNoWritableDatabase:
      return GCONF_ERROR_NO_WRITABLE_DATABASE;
      break;
    case ConfigInShutdown:
      return GCONF_ERROR_IN_SHUTDOWN;
      break;
    default:
      g_assert_not_reached();
      return GCONF_ERROR_SUCCESS; /* warnings */
      break;
    }
}

static gboolean
gconf_server_broken(CORBA_Environment* ev)
{
  switch (ev->_major)
    {
    case CORBA_SYSTEM_EXCEPTION:
      return TRUE;
      break;

    case CORBA_USER_EXCEPTION:
      {
        ConfigException* ce;

        ce = CORBA_exception_value(ev);

        return ce->err_no == ConfigInShutdown;
      }
      break;
      
    default:
      return FALSE;
      break;
    }
}

static gboolean
gconf_handle_corba_exception(CORBA_Environment* ev, GError** err)
{
  switch (ev->_major)
    {
    case CORBA_NO_EXCEPTION:
      CORBA_exception_free(ev);
      return FALSE;
      break;
    case CORBA_SYSTEM_EXCEPTION:
      if (err)
        *err = gconf_error_new(GCONF_ERROR_NO_SERVER, _("CORBA error: %s"),
                               CORBA_exception_id(ev));
      CORBA_exception_free(ev);
      return TRUE;
      break;
    case CORBA_USER_EXCEPTION:
      {
        ConfigException* ce;

        ce = CORBA_exception_value(ev);

        if (err)
          *err = gconf_error_new(corba_errno_to_gconf_errno(ce->err_no),
                                 ce->message);
        CORBA_exception_free(ev);
        return TRUE;
      }
      break;
    default:
      g_assert_not_reached();
      return TRUE;
      break;
    }
}

/*
 * Enumeration conversions
 */

gboolean
gconf_string_to_enum (GConfEnumStringPair lookup_table[],
                      const gchar* str,
                      gint* enum_value_retloc)
{
  int i = 0;
  
  while (lookup_table[i].str != NULL)
    {
      if (g_strcasecmp(lookup_table[i].str, str) == 0)
        {
          *enum_value_retloc = lookup_table[i].enum_value;
          return TRUE;
        }

      ++i;
    }

  return FALSE;
}

const gchar*
gconf_enum_to_string (GConfEnumStringPair lookup_table[],
                      gint enum_value)
{
  int i = 0;
  
  while (lookup_table[i].str != NULL)
    {
      if (lookup_table[i].enum_value == enum_value)
        return lookup_table[i].str;

      ++i;
    }

  return NULL;
}

