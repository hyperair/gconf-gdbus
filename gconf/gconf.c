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
#include <liboaf/liboaf.h>


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

/* Returns TRUE if there was an error, frees exception, sets err */
static gboolean gconf_handle_corba_exception(CORBA_Environment* ev, GConfError** err);
/* just returns TRUE if there's an exception indicating the server is
   probably hosed; no side effects */
static gboolean gconf_server_broken(CORBA_Environment* ev);

/* Maximum number of times to try re-spawning the server if it's down. */
#define MAX_RETRIES 1

gboolean
gconf_key_check(const gchar* key, GConfError** err)
{
  gchar* why = NULL;

  if (!gconf_valid_key(key, &why))
    {
      if (err)
        *err = gconf_error_new(GCONF_BAD_KEY, _("`%s': %s"),
                               key, why);
      g_free(why);
      return FALSE;
    }
  return TRUE;
}

/* 
 * GConfPrivate
 */

typedef struct _GConfEnginePrivate GConfEnginePrivate;
typedef struct _CnxnTable CnxnTable;

struct _GConfEnginePrivate {
  guint refcount;
  /* If this is ConfigServer_invalid_context then this
     is a local engine and not registered remotely, and has
     no ctable */
  ConfigServer_Context context;
  CnxnTable* ctable;
  /* If non-NULL, this is a local engine;
     local engines don't do notification! */
  GConfSources* local_sources;
};

static void register_engine(GConfEnginePrivate* priv);
static GConfEnginePrivate* lookup_engine(ConfigServer_Context context);
static void unregister_engine(GConfEnginePrivate* priv);
static gboolean reinstall_listeners_for_all_engines(ConfigServer cs,
                                                    GConfError** err);


typedef struct _GConfCnxn GConfCnxn;

struct _GConfCnxn {
  gchar* namespace_section;
  guint client_id;
  CORBA_unsigned_long server_id; /* id returned from server */
  GConfEngine* conf;     /* conf we're associated with */
  GConfNotifyFunc func;
  gpointer user_data;
};

static GConfCnxn* gconf_cnxn_new(GConfEngine* conf, const gchar* namespace_section, CORBA_unsigned_long server_id, GConfNotifyFunc func, gpointer user_data);
static void       gconf_cnxn_destroy(GConfCnxn* cnxn);
static void       gconf_cnxn_notify(GConfCnxn* cnxn, const gchar* key, GConfValue* value, gboolean is_default);

static ConfigServer gconf_get_config_server(gboolean start_if_not_found, GConfError** err);
/* Forget our current server object reference, so the next call to
   gconf_get_config_server will have to try to respawn the server */
static void         gconf_detach_config_server(void);
static ConfigListener gconf_get_config_listener(void);

/* We'll use client-specific connection numbers to return to library
   users, so if gconfd dies we can transparently re-register all our
   listener functions.  */

struct _CnxnTable {
  /* Hash from server-returned connection ID to GConfCnxn */
  GHashTable* server_ids;
  /* Hash from our connection ID to GConfCnxn */
  GHashTable* client_ids;
};

static CnxnTable* ctable_new(void);
static void       ctable_destroy(CnxnTable* ct);
static void       ctable_insert(CnxnTable* ct, GConfCnxn* cnxn);
static void       ctable_remove(CnxnTable* ct, GConfCnxn* cnxn);
static void       ctable_remove_by_client_id(CnxnTable* ct, guint client_id);
static GSList*    ctable_remove_by_conf(CnxnTable* ct, GConfEngine* conf);
static GConfCnxn* ctable_lookup_by_client_id(CnxnTable* ct, guint client_id);
static GConfCnxn* ctable_lookup_by_server_id(CnxnTable* ct, CORBA_unsigned_long server_id);
/* used after server re-spawn */
static gboolean   ctable_reinstall_everything(CnxnTable* ct,
                                              ConfigServer_Context context,
                                              ConfigServer cs,
                                              GConfError** err);

static GConfEnginePrivate*
gconf_engine_blank (gboolean remote)
{
  GConfEnginePrivate* priv;

  priv = g_new0(GConfEnginePrivate, 1);

  priv->refcount = 1;
  
  if (remote)
    {
      priv->context = ConfigServer_default_context;
      priv->ctable = ctable_new();
      priv->local_sources = NULL;
    }
  else
    {
      priv->context = ConfigServer_invalid_context;
      priv->ctable = NULL;
      priv->local_sources = NULL;
    }
    
  return priv;
}

static gboolean
gconf_engine_is_local(GConfEngine* conf)
{
  GConfEnginePrivate* priv = (GConfEnginePrivate*)conf;

  return (priv->context == ConfigServer_invalid_context);
}

/*
 *  Public Interface
 */

GConfEngine*
gconf_engine_new_local      (const gchar* address,
                             GConfError** err)
{
  GConfEnginePrivate* priv;
  GConfSource* source;

  g_return_val_if_fail(address != NULL, NULL);
  g_return_val_if_fail(err == NULL || *err == NULL, NULL);
  
  source = gconf_resolve_address(address, err);

  if (source == NULL)
    return NULL;
  
  priv = gconf_engine_blank(FALSE);

  priv->local_sources = gconf_sources_new_from_source(source);

  return (GConfEngine*)priv;
}

GConfEngine*
gconf_engine_new            (void)
{
  GConfEnginePrivate* priv;

  priv = gconf_engine_blank(TRUE);

  register_engine(priv);

  return (GConfEngine*)priv;
}

GConfEngine*
gconf_engine_new_from_address(const gchar* address, GConfError** err)
{
  GConfEngine* gconf;
  GConfEnginePrivate* priv;
  CORBA_Environment ev;
  ConfigServer cs;
  ConfigServer_Context ctx;
  int tries = 0;

  g_warning("Non-default GConfEngine's are basically broken, best not to use them right now.");
  
  CORBA_exception_init(&ev);
  
 RETRY:
  
  cs = gconf_get_config_server(TRUE, err);

  if (cs == CORBA_OBJECT_NIL)
    return NULL; /* Error should already be set */
  
  ctx = ConfigServer_get_context(cs, (gchar*)address, &ev);

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
    return NULL;
  
  if (ctx == ConfigServer_invalid_context)
    {
      if (err)
        *err = gconf_error_new(GCONF_BAD_ADDRESS,
                                _("Server couldn't resolve the address `%s'"),
                                address);

      return NULL;
    }
  
  priv = gconf_engine_blank(TRUE);
  
  gconf = (GConfEngine*)priv;

  priv->context = ctx;

  register_engine(priv);
  
  return gconf;
}

void
gconf_engine_ref             (GConfEngine* conf)
{
  GConfEnginePrivate* priv = (GConfEnginePrivate*)conf;

  g_return_if_fail(priv != NULL);
  g_return_if_fail(priv->refcount > 0);

  priv->refcount += 1;
}

void         
gconf_engine_unref        (GConfEngine* conf)
{
  GConfEnginePrivate* priv = (GConfEnginePrivate*)conf;
  
  g_return_if_fail(priv != NULL);
  g_return_if_fail(priv->refcount > 0);

  priv->refcount -= 1;
  
  if (priv->refcount == 0)
    {
      if (gconf_engine_is_local(conf))
        {
          if (priv->local_sources != NULL)
            gconf_sources_destroy(priv->local_sources);
        }
      else
        {
          /* Remove all connections associated with this GConf */
          GSList* removed;
          GSList* tmp;
          CORBA_Environment ev;
          ConfigServer cs;

          cs = gconf_get_config_server(FALSE, NULL); /* don't restart it
                                                        if down, since
                                                        the new one won't
                                                        have the
                                                        connections to
                                                        remove */
      
          CORBA_exception_init(&ev);

          /* FIXME CnxnTable only has entries for this GConfEngine now,
           * it used to be global and shared among GConfEngine objects.
           */
          removed = ctable_remove_by_conf(priv->ctable, conf);
  
          tmp = removed;
          while (tmp != NULL)
            {
              GConfCnxn* gcnxn = tmp->data;

              if (cs != CORBA_OBJECT_NIL)
                {
                  GConfError* err = NULL;
              
                  ConfigServer_remove_listener(cs,
                                               priv->context,
                                               gcnxn->server_id,
                                               &ev);

                  if (gconf_handle_corba_exception(&ev, &err))
                    {
                      /* Don't set error because realistically this doesn't matter to 
                         clients */
                      g_warning("Failure removing listener %u from the config server: %s",
                                (guint)gcnxn->server_id,
                                err->str);
                    }
                }

              gconf_cnxn_destroy(gcnxn);

              tmp = g_slist_next(tmp);
            }

          g_slist_free(removed);

          /* do this after removing the notifications,
             to avoid funky race conditions */
          unregister_engine(priv);
      
          ctable_destroy(priv->ctable);
        }
      
      g_free(priv);
    }
}

guint
gconf_notify_add(GConfEngine* conf,
                  const gchar* namespace_section, /* dir or key to listen to */
                  GConfNotifyFunc func,
                  gpointer user_data,
                  GConfError** err)
{
  GConfEnginePrivate* priv = (GConfEnginePrivate*)conf;
  ConfigServer cs;
  ConfigListener cl;
  gulong id;
  CORBA_Environment ev;
  GConfCnxn* cnxn;
  gint tries = 0;

  g_return_val_if_fail(!gconf_engine_is_local(conf), 0);
  
  if (gconf_engine_is_local(conf))
    {
      if (err)
        *err = gconf_error_new(GCONF_LOCAL_ENGINE,
                               _("Can't add notifications to a local configuration source"));

      return 0;
    }
  
  CORBA_exception_init(&ev);

 RETRY:
  
  cs = gconf_get_config_server(TRUE, err);

  if (cs == CORBA_OBJECT_NIL)
    return 0;

  cl = gconf_get_config_listener();
  
  /* Should have aborted the program in this case probably */
  g_return_val_if_fail(cl != CORBA_OBJECT_NIL, 0);

  id = ConfigServer_add_listener(cs, priv->context,
                                 (gchar*)namespace_section, 
                                 cl, &ev);
  
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
    return 0;

  cnxn = gconf_cnxn_new(conf, namespace_section, id, func, user_data);

  ctable_insert(priv->ctable, cnxn);

  return cnxn->client_id;
}

void         
gconf_notify_remove(GConfEngine* conf,
                    guint client_id)
{
  GConfEnginePrivate* priv = (GConfEnginePrivate*)conf;
  GConfCnxn* gcnxn;
  CORBA_Environment ev;
  ConfigServer cs;
  gint tries = 0;

  if (gconf_engine_is_local(conf))
    return;
  
  CORBA_exception_init(&ev);

 RETRY:
  
  cs = gconf_get_config_server(TRUE, NULL);

  if (cs == CORBA_OBJECT_NIL)
    return;

  gcnxn = ctable_lookup_by_client_id(priv->ctable, client_id);

  g_return_if_fail(gcnxn != NULL);

  ConfigServer_remove_listener(cs, priv->context,
                               gcnxn->server_id,
                               &ev);

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
  
  if (gconf_handle_corba_exception(&ev, NULL))
    {
      ; /* do nothing */
    }
  

  /* We want to do this even if the CORBA fails, so if we restart gconfd and 
     reinstall listeners we don't reinstall this one. */
  ctable_remove(priv->ctable, gcnxn);

  gconf_cnxn_destroy(gcnxn);
}

GConfValue*
gconf_get_full(GConfEngine* conf,
               const gchar* key, const gchar* locale,
               gboolean use_schema_default,
               gboolean* value_is_default,
               GConfError** err)
{
  GConfEnginePrivate* priv = (GConfEnginePrivate*)conf;
  GConfValue* val;
  ConfigValue* cv;
  CORBA_Environment ev;
  ConfigServer cs;
  gint tries = 0;
  CORBA_boolean is_default = FALSE;

  g_return_val_if_fail(conf != NULL, NULL);
  g_return_val_if_fail(key != NULL, NULL);
  g_return_val_if_fail(err == NULL || *err == NULL, NULL);
  
  if (!gconf_key_check(key, err))
    return NULL;

  if (gconf_engine_is_local(conf))
    {
      gchar** locale_list;

      locale_list = gconf_split_locale(locale);
      
      val = gconf_sources_query_value(priv->local_sources,
                                      key,
                                      (const gchar**)locale_list,
                                      use_schema_default,
                                      value_is_default,
                                      err);

      if (locale_list != NULL)
        g_strfreev(locale_list);
      
      return val;
    }

  g_assert(!gconf_engine_is_local(conf));
  
  CORBA_exception_init(&ev);

 RETRY:
  
  cs = gconf_get_config_server(TRUE, err);

  if (cs == CORBA_OBJECT_NIL)
    {
      g_return_val_if_fail(err == NULL || *err != NULL, NULL);

      return NULL;
    }

  cv = ConfigServer_lookup_with_locale(cs, priv->context,
                                       (gchar*)key, (gchar*)
                                       (locale ? locale : gconf_current_locale()),
                                       use_schema_default,
                                       &is_default,
                                       &ev);

  if (value_is_default)
    *value_is_default = !!is_default; /* canonicalize */

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
    {
      /* NOTE: don't free cvs since we got an exception! */
      return NULL;
    }
  else
    {
      val = gconf_value_from_corba_value(cv);
      CORBA_free(cv);

      return val;
    }
}
     
GConfValue*  
gconf_get(GConfEngine* conf, const gchar* key, GConfError** err)
{
  return gconf_get_with_locale(conf, key, NULL, err);
}

GConfValue*
gconf_get_with_locale(GConfEngine* conf, const gchar* key, const gchar* locale,
                      GConfError** err)
{
  return gconf_get_full(conf, key, locale, TRUE, NULL, err);
}

GConfValue*
gconf_get_without_default(GConfEngine* conf, const gchar* key, GConfError** err)
{
  return gconf_get_full(conf, key, NULL, FALSE, NULL, err);
}

GConfValue*
gconf_get_default_from_schema (GConfEngine* conf,
                               const gchar* key,
                               GConfError** err)
{
  GConfEnginePrivate* priv = (GConfEnginePrivate*)conf;
  GConfValue* val;
  ConfigValue* cv;
  CORBA_Environment ev;
  ConfigServer cs;
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
      
      val = gconf_sources_query_default_value(priv->local_sources,
                                              key,
                                              (const gchar**)locale_list,
                                              err);

      if (locale_list != NULL)
        g_strfreev(locale_list);
      
      return val;
    }

  g_assert(!gconf_engine_is_local(conf));
  
  CORBA_exception_init(&ev);

 RETRY:
  
  cs = gconf_get_config_server(TRUE, err);

  if (cs == CORBA_OBJECT_NIL)
    {
      g_return_val_if_fail(err == NULL || *err != NULL, NULL);

      return NULL;
    }

  cv = ConfigServer_lookup_default_value(cs, priv->context,
                                         (gchar*)key,
                                         (gchar*)gconf_current_locale(),
                                         &ev);
  
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
gconf_set(GConfEngine* conf, const gchar* key, GConfValue* value, GConfError** err)
{
  GConfEnginePrivate* priv = (GConfEnginePrivate*)conf;
  ConfigValue* cv;
  CORBA_Environment ev;
  ConfigServer cs;
  gint tries = 0;

  g_return_val_if_fail(conf != NULL, FALSE);
  g_return_val_if_fail(key != NULL, FALSE);
  g_return_val_if_fail(value != NULL, FALSE);
  g_return_val_if_fail(value->type != GCONF_VALUE_INVALID, FALSE);
  g_return_val_if_fail( (value->type != GCONF_VALUE_STRING) ||
                        (gconf_value_string(value) != NULL) , FALSE );
  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);
  
  if (!gconf_key_check(key, err))
    return FALSE;

  if (gconf_engine_is_local(conf))
    {
      GConfError* error = NULL;
      
      gconf_sources_set_value(priv->local_sources, key, value, &error);

      if (error != NULL)
        {
          if (err)
            *err = error;
          else
            {
              gconf_error_destroy(error);
            }
          return FALSE;
        }
      
      return TRUE;
    }

  g_assert(!gconf_engine_is_local(conf));
  
  CORBA_exception_init(&ev);

 RETRY:
  
  cs = gconf_get_config_server(TRUE, err);

  if (cs == CORBA_OBJECT_NIL)
    {
      g_return_val_if_fail(err == NULL || *err != NULL, FALSE);

      return FALSE;
    }

  cv = corba_value_from_gconf_value(value);

  ConfigServer_set(cs, priv->context,
                   (gchar*)key, cv,
                   &ev);

  CORBA_free(cv);

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

  g_return_val_if_fail(*err == NULL, FALSE);
  
  return TRUE;
}

gboolean
gconf_unset(GConfEngine* conf, const gchar* key, GConfError** err)
{
  GConfEnginePrivate* priv = (GConfEnginePrivate*)conf;
  CORBA_Environment ev;
  ConfigServer cs;
  gint tries = 0;

  g_return_val_if_fail(conf != NULL, FALSE);
  g_return_val_if_fail(key != NULL, FALSE);
  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);
  
  if (!gconf_key_check(key, err))
    return FALSE;

  if (gconf_engine_is_local(conf))
    {
      GConfError* error = NULL;
      
      gconf_sources_unset_value(priv->local_sources, key, NULL, &error);

      if (error != NULL)
        {
          if (err)
            *err = error;
          else
            {
              gconf_error_destroy(error);
            }
          return FALSE;
        }
      
      return TRUE;
    }

  g_assert(!gconf_engine_is_local(conf));
  
  CORBA_exception_init(&ev);

 RETRY:
  
  cs = gconf_get_config_server(TRUE, err);

  if (cs == CORBA_OBJECT_NIL)
    {
      g_return_val_if_fail(err == NULL || *err != NULL, FALSE);

      return FALSE;
    }

  ConfigServer_unset(cs, priv->context,
                     (gchar*)key,
                     &ev);

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

  g_return_val_if_fail(*err == NULL, FALSE);
  
  return TRUE;
}

gboolean
gconf_associate_schema  (GConfEngine* conf, const gchar* key,
                         const gchar* schema_key, GConfError** err)
{
  GConfEnginePrivate* priv = (GConfEnginePrivate*)conf;
  CORBA_Environment ev;
  ConfigServer cs;
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
      GConfError* error = NULL;
      
      gconf_sources_set_schema(priv->local_sources, key, schema_key, &error);

      if (error != NULL)
        {
          if (err)
            *err = error;
          else
            {
              gconf_error_destroy(error);
            }
          return FALSE;
        }
      
      return TRUE;
    }

  g_assert(!gconf_engine_is_local(conf));
  
  CORBA_exception_init(&ev);

 RETRY:
  
  cs = gconf_get_config_server(TRUE, err);

  if (cs == CORBA_OBJECT_NIL)
    {
      g_return_val_if_fail(err == NULL || *err != NULL, FALSE);

      return FALSE;
    }

  ConfigServer_set_schema(cs, priv->context,
                          (gchar*)key,
                          (gchar*)schema_key,
                          &ev);

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

  g_return_val_if_fail(*err == NULL, FALSE);
  
  return TRUE;
}

GSList*      
gconf_all_entries(GConfEngine* conf, const gchar* dir, GConfError** err)
{
  GConfEnginePrivate* priv = (GConfEnginePrivate*)conf;
  GSList* pairs = NULL;
  ConfigServer_ValueList* values;
  ConfigServer_KeyList* keys;
  ConfigServer_IsDefaultList* is_defaults;
  CORBA_Environment ev;
  ConfigServer cs;
  guint i;
  gint tries = 0;

  g_return_val_if_fail(conf != NULL, NULL);
  g_return_val_if_fail(dir != NULL, NULL);
  g_return_val_if_fail(err == NULL || *err == NULL, NULL);
  
  if (!gconf_key_check(dir, err))
    return NULL;


  if (gconf_engine_is_local(conf))
    {
      GConfError* error = NULL;
      gchar** locale_list;
      GSList* retval;
      
      locale_list = gconf_split_locale(gconf_current_locale());
      
      retval = gconf_sources_all_entries(priv->local_sources,
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
              gconf_error_destroy(error);
            }

          g_assert(retval == NULL);
          
          return NULL;
        }
      
      return retval;
    }

  g_assert(!gconf_engine_is_local(conf));
  
  CORBA_exception_init(&ev);
  
 RETRY:
  
  cs = gconf_get_config_server(TRUE, err);

  if (cs == CORBA_OBJECT_NIL)
    {
      g_return_val_if_fail(err == NULL || *err != NULL, NULL);

      return NULL;
    }
  
  ConfigServer_all_entries(cs, priv->context,
                           (gchar*)dir,
                           (gchar*)gconf_current_locale(),
                           &keys, &values, &is_defaults,
                           &ev);

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
    return NULL;
  
  if (keys->_length != values->_length)
    {
      g_warning("Received unmatched key/value sequences in %s",
                __FUNCTION__);
      return NULL;
    }

  i = 0;
  while (i < keys->_length)
    {
      GConfEntry* pair;

      pair = 
        gconf_entry_new_nocopy(g_strdup(keys->_buffer[i]),
                               gconf_value_from_corba_value(&(values->_buffer[i])));

      /* note, there's an accesor function for setting this that we are
         cheating and not using */
      pair->is_default = is_defaults->_buffer[i];
      
      pairs = g_slist_prepend(pairs, pair);
      
      ++i;
    }
  
  CORBA_free(keys);
  CORBA_free(values);
  CORBA_free(is_defaults);

  return pairs;
}

GSList*      
gconf_all_dirs(GConfEngine* conf, const gchar* dir, GConfError** err)
{
  GConfEnginePrivate* priv = (GConfEnginePrivate*)conf;
  GSList* subdirs = NULL;
  ConfigServer_KeyList* keys;
  CORBA_Environment ev;
  ConfigServer cs;
  guint i;
  gint tries = 0;

  g_return_val_if_fail(conf != NULL, NULL);
  g_return_val_if_fail(dir != NULL, NULL);
  g_return_val_if_fail(err == NULL || *err == NULL, NULL);
  
  if (!gconf_key_check(dir, err))
    return NULL;

  if (gconf_engine_is_local(conf))
    {
      GConfError* error = NULL;
      GSList* retval;
      
      retval = gconf_sources_all_dirs(priv->local_sources,
                                      dir,
                                      &error);
      
      if (error != NULL)
        {
          if (err)
            *err = error;
          else
            {
              gconf_error_destroy(error);
            }

          g_assert(retval == NULL);
          
          return NULL;
        }
      
      return retval;
    }

  g_assert(!gconf_engine_is_local(conf));
  
  CORBA_exception_init(&ev);
  
 RETRY:
  
  cs = gconf_get_config_server(TRUE, err);

  if (cs == CORBA_OBJECT_NIL)
    {
      g_return_val_if_fail(((err == NULL) || (*err && ((*err)->num == GCONF_NO_SERVER))), NULL);

      return NULL;
    }
  
  ConfigServer_all_dirs(cs, priv->context,
                        (gchar*)dir, 
                        &keys,
                        &ev);

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
    return NULL;
  
  i = 0;
  while (i < keys->_length)
    {
      gchar* s;

      s = g_strdup(keys->_buffer[i]);
      
      subdirs = g_slist_prepend(subdirs, s);
      
      ++i;
    }
  
  CORBA_free(keys);

  return subdirs;
}

/* annoyingly, this is REQUIRED for local sources */
void 
gconf_suggest_sync(GConfEngine* conf, GConfError** err)
{
  GConfEnginePrivate* priv = (GConfEnginePrivate*)conf;
  CORBA_Environment ev;
  ConfigServer cs;
  gint tries = 0;

  g_return_if_fail(conf != NULL);
  g_return_if_fail(err == NULL || *err == NULL);

  if (gconf_engine_is_local(conf))
    {
      GConfError* error = NULL;
      
      gconf_sources_sync_all(priv->local_sources,
                             &error);
      
      if (error != NULL)
        {
          if (err)
            *err = error;
          else
            {
              gconf_error_destroy(error);
            }
          return;
        }
      
      return;
    }

  g_assert(!gconf_engine_is_local(conf));
  
  CORBA_exception_init(&ev);

 RETRY:
  
  cs = gconf_get_config_server(TRUE, err);

  if (cs == CORBA_OBJECT_NIL)
    {
      g_return_if_fail(err == NULL || *err != NULL);

      return;
    }

  ConfigServer_sync(cs, priv->context, &ev);

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
    ; /* nothing additional */
}

gboolean
gconf_dir_exists(GConfEngine *conf, const gchar *dir, GConfError** err)
{
  GConfEnginePrivate* priv = (GConfEnginePrivate*)conf;
  CORBA_Environment ev;
  ConfigServer cs;
  CORBA_boolean server_ret;
  gint tries = 0;

  g_return_val_if_fail(conf != NULL, FALSE);
  g_return_val_if_fail(dir != NULL, FALSE);
  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);
  
  if (!gconf_key_check(dir, err))
    return FALSE;
  
  if (gconf_engine_is_local(conf))
    {
      return gconf_sources_dir_exists(priv->local_sources,
                                      dir,
                                      err);
    }

  g_assert(!gconf_engine_is_local(conf));
  
  CORBA_exception_init(&ev);
  
 RETRY:
  
  cs = gconf_get_config_server(TRUE, err);
  
  if (cs == CORBA_OBJECT_NIL)
    {
      g_return_val_if_fail(err == NULL || *err != NULL, FALSE);

      return FALSE;
    }
  
  server_ret = ConfigServer_dir_exists(cs, priv->context,
                                       (gchar*)dir, &ev);
  
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
    ; /* nothing */

  return (server_ret == CORBA_TRUE);
}

/*
 * Connection maintenance
 */

static GConfCnxn* 
gconf_cnxn_new(GConfEngine* conf, const gchar* namespace_section, CORBA_unsigned_long server_id, GConfNotifyFunc func, gpointer user_data)
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
                  const gchar* key, GConfValue* value, gboolean is_default)
{
  (*cnxn->func)(cnxn->conf, cnxn->client_id, key, value, is_default, cnxn->user_data);
}

/*
 *  CORBA glue
 */

static ConfigServer   server = CORBA_OBJECT_NIL;

/* errors in here should be GCONF_NO_SERVER */
static ConfigServer
try_to_contact_server(gboolean start_if_not_found, GConfError** err)
{
  CORBA_Environment ev;
  OAF_ActivationFlags flags;
  
  CORBA_exception_init(&ev);

  flags = 0;
  if (!start_if_not_found)
    flags |= OAF_FLAG_EXISTING_ONLY;
  
  server = oaf_activate_from_id("OAFAID:["IID"]", flags, NULL, &ev);

  /* So try to ping server */
  if (!CORBA_Object_is_nil(server, &ev))
    {
      ConfigServer_ping(server, &ev);
      
      if (ev._major != CORBA_NO_EXCEPTION)
	{
	  server = CORBA_OBJECT_NIL;
	  if (err)
	    *err = gconf_error_new(GCONF_NO_SERVER, _("Pinging the server failed, CORBA error: %s"),
				   CORBA_exception_id(&ev));

          CORBA_exception_free(&ev);
	}
    }
  else
    {
      if (gconf_handle_oaf_exception(&ev, err))
        {
          /* Make the errno more specific */
          if (err && *err)
            (*err)->num = GCONF_NO_SERVER;
          else if (err && !*err)
            *err = gconf_error_new(GCONF_NO_SERVER, _("Error contacting configuration server: OAF returned nil from oaf_activate_from_id() and did not set an exception explaining the problem. Please file an OAF bug report."));
        }
    }

#ifdef GCONF_ENABLE_DEBUG      
  if (server == CORBA_OBJECT_NIL && start_if_not_found)
    {
      g_return_val_if_fail(err == NULL || *err != NULL, server);
    }
#endif
  
  return server;
}

/* All errors set in here should be GCONF_NO_SERVER; should
   only set errors if start_if_not_found is TRUE */
static ConfigServer
gconf_get_config_server(gboolean start_if_not_found, GConfError** err)
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

static void 
notify(PortableServer_Servant servant,
       CORBA_unsigned_long context,
       CORBA_unsigned_long cnxn,
       const CORBA_char* key, 
       const ConfigValue* value,
       CORBA_boolean is_default,
       CORBA_Environment *ev);

static PortableServer_ServantBase__epv base_epv = {
  NULL,
  NULL,
  NULL
};

static POA_ConfigListener__epv listener_epv = { NULL, notify };
static POA_ConfigListener__vepv poa_listener_vepv = { &base_epv, &listener_epv };
static POA_ConfigListener poa_listener_servant = { NULL, &poa_listener_vepv };

static void 
notify(PortableServer_Servant servant,
       CORBA_unsigned_long context,
       CORBA_unsigned_long server_id,
       const CORBA_char* key,
       const ConfigValue* value,
       CORBA_boolean is_default,
       CORBA_Environment *ev)
{
  GConfCnxn* cnxn;
  GConfValue* gvalue;
  GConfEnginePrivate* priv;

  priv = lookup_engine(context);

  if (priv == NULL)
    {
      g_warning("Client received notify for unknown context %u",
                (guint)context);
      return;
    }
  
  cnxn = ctable_lookup_by_server_id(priv->ctable, server_id);
  
  if (cnxn == NULL)
    {
      g_warning("Client received notify for unknown connection ID %u",
                (guint)server_id);
      return;
    }

  gvalue = gconf_value_from_corba_value(value);

  gconf_cnxn_notify(cnxn, key, gvalue, is_default);

  if (gvalue != NULL)
    gconf_value_destroy(gvalue);
}

static ConfigListener 
gconf_get_config_listener(void)
{
  return listener;
}

static gboolean have_initted = FALSE;

void
gconf_preinit(gpointer app, gpointer mod_info)
{
}

void
gconf_postinit(gpointer app, gpointer mod_info)
{
  if (listener == CORBA_OBJECT_NIL)
    {
      CORBA_Environment ev;
      PortableServer_ObjectId objid = {0, sizeof("ConfigListener"), "ConfigListener"};
      PortableServer_POA poa;

      CORBA_exception_init(&ev);
      POA_ConfigListener__init(&poa_listener_servant, &ev);
      
      g_assert (ev._major == CORBA_NO_EXCEPTION);

      poa = (PortableServer_POA)CORBA_ORB_resolve_initial_references(oaf_orb_get(), "RootPOA", &ev);

      g_assert (ev._major == CORBA_NO_EXCEPTION);

      PortableServer_POAManager_activate(PortableServer_POA__get_the_POAManager(poa, &ev), &ev);

      g_assert (ev._major == CORBA_NO_EXCEPTION);

      PortableServer_POA_activate_object_with_id(poa,
                                                 &objid, &poa_listener_servant, &ev);

      g_assert (ev._major == CORBA_NO_EXCEPTION);
      
      listener = PortableServer_POA_servant_to_reference(poa,
                                                         &poa_listener_servant,
                                                         &ev);

      g_assert (listener != CORBA_OBJECT_NIL);
      g_assert (ev._major == CORBA_NO_EXCEPTION);
    }

  have_initted = TRUE;
}

const char gconf_version[] = VERSION;

struct poptOption gconf_options[] = {
  {NULL}
};

gboolean     
gconf_init           (int argc, char **argv, GConfError** err)
{
  CORBA_ORB orb = CORBA_OBJECT_NIL;

  if (have_initted)
    {
      g_warning("Attempt to init GConf a second time");
      return FALSE;
    }

  gconf_preinit(NULL, NULL);

  if (!oaf_is_initialized())
    {
      orb = oaf_init(argc, argv);
    }
  else
    {
      orb = oaf_orb_get();
    }
      
  gconf_postinit(NULL, NULL);

  if(!have_initted)
    return FALSE;
  
  return TRUE;
}

gboolean
gconf_is_initialized (void)
{
  return have_initted;
}

/* 
 * Ampersand and <> are not allowed due to the XML backend; shell
 * special characters aren't allowed; others are just in case we need
 * some magic characters someday.  hyphen, underscore, period, colon
 * are allowed as separators. % disallowed to avoid printf confusion.
 */

/* Key/dir validity is exactly the same, except that '/' must be a dir, 
   but we are sort of ignoring that for now. */

static const gchar invalid_chars[] = "\"$&<>,+=#!()'|{}[]?~`;%\\";

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
  return strncmp(below, above, strlen(above)) == 0;
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
                        (guint)&key);

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

  ct->server_ids = g_hash_table_new(corba_unsigned_long_hash, corba_unsigned_long_equal);  
  ct->client_ids = g_hash_table_new(g_int_hash, g_int_equal);

  return ct;
}

static void
ctable_destroy(CnxnTable* ct)
{
  g_hash_table_destroy(ct->server_ids);
  g_hash_table_destroy(ct->client_ids);
  g_free(ct);
}

static void       
ctable_insert(CnxnTable* ct, GConfCnxn* cnxn)
{
  g_hash_table_insert(ct->server_ids, &cnxn->server_id, cnxn);
  g_hash_table_insert(ct->client_ids, &cnxn->client_id, cnxn);
}

static void       
ctable_remove(CnxnTable* ct, GConfCnxn* cnxn)
{
  g_hash_table_remove(ct->server_ids, &cnxn->server_id);
  g_hash_table_remove(ct->client_ids, &cnxn->client_id);
}

static void       
ctable_remove_by_client_id(CnxnTable* ct, guint client_id)
{
  GConfCnxn* cnxn;

  cnxn = ctable_lookup_by_client_id(ct, client_id);

  g_return_if_fail(cnxn != NULL);

  ctable_remove(ct, cnxn);
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
  
  client_ids_removed = g_hash_table_foreach_remove(ct->server_ids, remove_by_conf, &rd);

  rd.save_removed = FALSE;

  server_ids_removed = g_hash_table_foreach_remove(ct->client_ids, remove_by_conf, &rd);

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
  return g_hash_table_lookup(ct->server_ids, &server_id);
}

struct ReinstallData {
  CORBA_Environment ev;
  ConfigListener cl;
  ConfigServer cs;
  GConfError* error;
  ConfigServer_Context context;
};

static void
reinstall_foreach(gpointer key, gpointer value, gpointer user_data)
{
  GConfCnxn* cnxn;
  struct ReinstallData* rd;

  rd = user_data;
  cnxn = value;

  g_assert(rd != NULL);
  g_assert(cnxn != NULL);

  /* invalidate the server id, but take no
     further action - something is badly screwed.
  */
  if (rd->error != NULL)
    {
      cnxn->server_id = 0;
      return;
    }
    
  cnxn->server_id = ConfigServer_add_listener(rd->cs, rd->context,
                                              cnxn->namespace_section, 
                                              rd->cl, &(rd->ev));
  
  if (gconf_handle_corba_exception(&(rd->ev), &(rd->error)))
    ; /* just let error get set */
}

static void
insert_in_server_ids_foreach(gpointer key, gpointer value, gpointer user_data)
{
  GHashTable* server_ids;
  GConfCnxn* cnxn;

  cnxn = value;
  server_ids = user_data;

  g_assert(cnxn != NULL);
  g_assert(server_ids != NULL);

  /* ensure we have a valid server id */
  g_return_if_fail(cnxn->server_id != 0);
  
  g_hash_table_insert(server_ids, &cnxn->server_id, cnxn);
}

static gboolean
ctable_reinstall_everything(CnxnTable* ct, ConfigServer_Context context,
                            ConfigServer cs, GConfError** err)
{
  ConfigListener cl;
  GConfCnxn* cnxn;
  struct ReinstallData rd;

  g_return_val_if_fail(cs != CORBA_OBJECT_NIL, FALSE);
  
  cl = gconf_get_config_listener();

  /* Should have aborted the program in this case probably */
  g_return_val_if_fail(cl != CORBA_OBJECT_NIL, FALSE);

  /* First we re-install everything in the client-id-to-cnxn hash,
     then blow away the server-id-to-cnxn hash and re-insert everything
     from the client-id-to-cnxn hash.
  */

  CORBA_exception_init(&rd.ev);
  rd.cl = cl;
  rd.cs = cs;
  rd.error = NULL;
  rd.context = context;

  g_hash_table_foreach(ct->client_ids, reinstall_foreach, &rd);

  if (rd.error != NULL)
    {
      if (err != NULL)
        *err = rd.error;
      else
        {
          gconf_error_destroy(rd.error);
        }
      /* Note that the table is now in an inconsistent state; some
         notifications may be re-installed, some may not.  Those that
         were not now have the invalid server id 0.  However, if there
         was an error, it was probably due to a crashed server
         (GCONF_NO_SERVER), so we should re-install everything.
      */
      return FALSE;
    }

  g_hash_table_destroy(ct->server_ids);

  ct->server_ids = g_hash_table_new(corba_unsigned_long_hash, corba_unsigned_long_equal);

  g_hash_table_foreach(ct->client_ids, insert_in_server_ids_foreach, ct->server_ids);

  return TRUE;
}

/*
 * Daemon control
 */

void          
gconf_shutdown_daemon(GConfError** err)
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
        *err = gconf_error_new(GCONF_FAILED, _("Failure shutting down config server: %s"),
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
gconf_spawn_daemon(GConfError** err)
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
gconf_get_float (GConfEngine* conf, const gchar* key,
                 GConfError** err)
{
  GConfValue* val;
  static const gdouble deflt = 0.0;
  
  g_return_val_if_fail(conf != NULL, 0.0);
  g_return_val_if_fail(key != NULL, 0.0);
  
  val = gconf_get(conf, key, err);

  if (val == NULL)
    return deflt;
  else
    {
      gdouble retval;
      
      if (val->type != GCONF_VALUE_FLOAT)
        {
          if (err)
            *err = gconf_error_new(GCONF_TYPE_MISMATCH, _("Expected float, got %s"),
                                    gconf_value_type_to_string(val->type));
          gconf_value_destroy(val);
          return deflt;
        }

      retval = gconf_value_float(val);

      gconf_value_destroy(val);

      return retval;
    }
}

gint         
gconf_get_int   (GConfEngine* conf, const gchar* key,
                 GConfError** err)
{
  GConfValue* val;
  static const gint deflt = 0;
  
  g_return_val_if_fail(conf != NULL, 0);
  g_return_val_if_fail(key != NULL, 0);
  
  val = gconf_get(conf, key, err);

  if (val == NULL)
    return deflt;
  else
    {
      gint retval;

      if (val->type != GCONF_VALUE_INT)
        {
          if (err)
            *err = gconf_error_new(GCONF_TYPE_MISMATCH, _("Expected int, got %s"),
                                    gconf_value_type_to_string(val->type));
          gconf_value_destroy(val);
          return deflt;
        }

      retval = gconf_value_int(val);

      gconf_value_destroy(val);

      return retval;
    }
}

gchar*       
gconf_get_string(GConfEngine* conf, const gchar* key,
                 GConfError** err)
{
  GConfValue* val;
  static const gchar* deflt = NULL;
  
  g_return_val_if_fail(conf != NULL, NULL);
  g_return_val_if_fail(key != NULL, NULL);
  
  val = gconf_get(conf, key, err);

  if (val == NULL)
    return deflt ? g_strdup(deflt) : NULL;
  else
    {
      gchar* retval;

      if (val->type != GCONF_VALUE_STRING)
        {
          if (err)
            *err = gconf_error_new(GCONF_TYPE_MISMATCH, _("Expected string, got %s"),
                                    gconf_value_type_to_string(val->type));
          gconf_value_destroy(val);
          return deflt ? g_strdup(deflt) : NULL;
        }

      retval = val->d.string_data;
      /* This is a cheat; don't copy */
      val->d.string_data = NULL; /* don't delete the string */

      gconf_value_destroy(val);

      return retval;
    }
}

gboolean     
gconf_get_bool  (GConfEngine* conf, const gchar* key,
                 GConfError** err)
{
  GConfValue* val;
  static const gboolean deflt = FALSE;
  
  g_return_val_if_fail(conf != NULL, FALSE);
  g_return_val_if_fail(key != NULL, FALSE);
  
  val = gconf_get(conf, key, err);

  if (val == NULL)
    return deflt;
  else
    {
      gboolean retval;

      if (val->type != GCONF_VALUE_BOOL)
        {
          if (err)
            *err = gconf_error_new(GCONF_TYPE_MISMATCH, _("Expected bool, got %s"),
                                    gconf_value_type_to_string(val->type));
          gconf_value_destroy(val);
          return deflt;
        }

      retval = gconf_value_bool(val);

      gconf_value_destroy(val);

      return retval;
    }
}

GConfSchema* 
gconf_get_schema  (GConfEngine* conf, const gchar* key, GConfError** err)
{
  GConfValue* val;

  g_return_val_if_fail(conf != NULL, NULL);
  g_return_val_if_fail(key != NULL, NULL);
  
  val = gconf_get_with_locale(conf, key, gconf_current_locale(), err);

  if (val == NULL)
    return NULL;
  else
    {
      GConfSchema* retval;

      if (val->type != GCONF_VALUE_SCHEMA)
        {
          if (err)
            *err = gconf_error_new(GCONF_TYPE_MISMATCH, _("Expected schema, got %s"),
                                    gconf_value_type_to_string(val->type));
          gconf_value_destroy(val);
          return NULL;
        }

      retval = gconf_value_schema(val);

      /* This is a cheat; don't copy */
      val->d.schema_data = NULL; /* don't delete the schema */

      gconf_value_destroy(val);

      return retval;
    }
}

GSList*
gconf_get_list    (GConfEngine* conf, const gchar* key,
                   GConfValueType list_type, GConfError** err)
{
  GConfValue* val;

  g_return_val_if_fail(conf != NULL, NULL);
  g_return_val_if_fail(key != NULL, NULL);
  g_return_val_if_fail(list_type != GCONF_VALUE_INVALID, NULL);
  g_return_val_if_fail(list_type != GCONF_VALUE_LIST, NULL);
  g_return_val_if_fail(list_type != GCONF_VALUE_PAIR, NULL);
  g_return_val_if_fail(err == NULL || *err == NULL, NULL);
  
  val = gconf_get_with_locale(conf, key, gconf_current_locale(), err);

  if (val == NULL)
    return NULL;
  else
    {
      /* This type-checks the value */
      return gconf_value_list_to_primitive_list_destructive(val, list_type, err);
    }
}

gboolean
gconf_get_pair    (GConfEngine* conf, const gchar* key,
                   GConfValueType car_type, GConfValueType cdr_type,
                   gpointer car_retloc, gpointer cdr_retloc,
                   GConfError** err)
{
  GConfValue* val;
  GConfError* error = NULL;
  
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
  
  val = gconf_get_with_locale(conf, key, gconf_current_locale(), &error);

  if (error != NULL)
    {
      g_assert(val == NULL);
      
      if (err)
        *err = error;
      else
        gconf_error_destroy(error);

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
                  GConfValue* gval, GConfError** err)
{
  GConfError* my_err = NULL;
  
  gconf_set(conf, key, gval, &my_err);

  gconf_value_destroy(gval);
  
  if (my_err != NULL)
    {
      if (err)
        *err = my_err;
      else
        gconf_error_destroy(my_err);
      return FALSE;
    }
  else
    return TRUE;
}

gboolean
gconf_set_float   (GConfEngine* conf, const gchar* key,
                   gdouble val, GConfError** err)
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
gconf_set_int     (GConfEngine* conf, const gchar* key,
                   gint val, GConfError** err)
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
gconf_set_string  (GConfEngine* conf, const gchar* key,
                    const gchar* val, GConfError** err)
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
gconf_set_bool    (GConfEngine* conf, const gchar* key,
                   gboolean val, GConfError** err)
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
gconf_set_schema  (GConfEngine* conf, const gchar* key,
                    GConfSchema* val, GConfError** err)
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
gconf_set_list    (GConfEngine* conf, const gchar* key,
                   GConfValueType list_type,
                   GSList* list,
                   GConfError** err)
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
gconf_set_pair    (GConfEngine* conf, const gchar* key,
                   GConfValueType car_type, GConfValueType cdr_type,
                   gconstpointer address_of_car,
                   gconstpointer address_of_cdr,
                   GConfError** err)
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

/* Set GConfErrNo from an exception, free exception, etc. */

static GConfErrNo
corba_errno_to_gconf_errno(ConfigErrorType corba_err)
{
  switch (corba_err)
    {
    case ConfigFailed:
      return GCONF_FAILED;
      break;
    case ConfigNoPermission:
      return GCONF_NO_PERMISSION;
      break;
    case ConfigBadAddress:
      return GCONF_BAD_ADDRESS;
      break;
    case ConfigBadKey:
      return GCONF_BAD_KEY;
      break;
    case ConfigParseError:
      return GCONF_PARSE_ERROR;
      break;
    case ConfigCorrupt:
      return GCONF_CORRUPT;
      break;
    case ConfigTypeMismatch:
      return GCONF_TYPE_MISMATCH;
      break;
    case ConfigIsDir:
      return GCONF_IS_DIR;
      break;
    case ConfigIsKey:
      return GCONF_IS_KEY;
      break;
    case ConfigOverridden:
      return GCONF_OVERRIDDEN;
      break;
    case ConfigLockFailed:
      return GCONF_LOCK_FAILED;
      break;
    default:
      g_assert_not_reached();
      return GCONF_SUCCESS; /* warnings */
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
    default:
      return FALSE;
      break;
    }
}

static gboolean
gconf_handle_corba_exception(CORBA_Environment* ev, GConfError** err)
{
  switch (ev->_major)
    {
    case CORBA_NO_EXCEPTION:
      CORBA_exception_free(ev);
      return FALSE;
      break;
    case CORBA_SYSTEM_EXCEPTION:
      if (err)
        *err = gconf_error_new(GCONF_NO_SERVER, _("CORBA error: %s"),
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
 * context-to-engine map
 */

/* So we can find an engine from a context */
static GHashTable* context_to_engine_hash = NULL;

static void
register_engine(GConfEnginePrivate* priv)
{
  if (context_to_engine_hash == NULL)
    {
      context_to_engine_hash = g_hash_table_new(corba_unsigned_long_hash, corba_unsigned_long_equal);
    }
  
  g_hash_table_insert(context_to_engine_hash, &(priv->context), priv);
}

static GConfEnginePrivate*
lookup_engine(ConfigServer_Context context)
{
  g_return_val_if_fail(context_to_engine_hash != NULL, NULL);

  return g_hash_table_lookup(context_to_engine_hash, &context);
}

static void
unregister_engine(GConfEnginePrivate* priv)
{
  g_return_if_fail(context_to_engine_hash != NULL);
  
  g_hash_table_remove(context_to_engine_hash, &(priv->context));

  if (g_hash_table_size(context_to_engine_hash) == 0)
    {
      g_hash_table_destroy(context_to_engine_hash);
      context_to_engine_hash = NULL;
    }
}

struct EngineReinstallData {
  ConfigServer cs;
  GConfError* error;
};

static void
engine_reinstall_foreach(gpointer key, gpointer value, gpointer user_data)
{
  struct EngineReinstallData* erd;
  GConfEnginePrivate* priv;

  erd = user_data;
  priv = value;

  g_assert(erd != NULL);
  g_assert(priv != NULL);

  /* Bail if any errors have occurred */
  if (erd->error != NULL)
    return;
  
  ctable_reinstall_everything(priv->ctable, priv->context, erd->cs, &(erd->error));
}

static gboolean
reinstall_listeners_for_all_engines(ConfigServer cs,
                                    GConfError** err)
{
  struct EngineReinstallData erd;

  /* FIXME if the server died, our contexts our now invalid;
     so we need the auto-restore-contexts arrangement to handle
     that. In other words for now this function won't work.
  */
  
  erd.cs = cs;
  erd.error = NULL;

  g_hash_table_foreach(context_to_engine_hash, engine_reinstall_foreach, &erd);

  if (erd.error != NULL)
    {
      if (err != NULL)
        {
          *err = erd.error;
        }
      else
        {
          gconf_error_destroy(erd.error);
        }
      return FALSE;
    }

  return TRUE;
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

