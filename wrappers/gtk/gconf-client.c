/* -*- tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
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

#include "gconf-client.h"
#include <gtk/gtksignal.h>
#include <gtk/gtktypeutils.h>
#include <gconf/gconf-internals.h>

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


/*
 * CacheEntry
 */ 

typedef struct _CacheEntry CacheEntry;

struct _CacheEntry {
  GConfValue* value;
};

static CacheEntry* cache_entry_new(GConfValue* val);
static void        cache_entry_destroy(CacheEntry* ce);

/*
 * Dir object (for list of directories we're watching)
 */

typedef struct _Dir Dir;

struct _Dir {
  gchar* name;
  guint notify_id;

};

static Dir* dir_new(const gchar* name, guint notify_id);
static void dir_destroy(Dir* d);

/*
 * Listener object
 */

typedef struct _Listener Listener;

struct _Listener {
  GConfClientNotifyFunc func;
  gpointer data;
  GFreeFunc destroy_notify;
};

static Listener* listener_new(GConfClientNotifyFunc func,
                              GFreeFunc destroy_notify,
                              gpointer data);

static void listener_destroy(Listener* l);

/*
 * GConfClient proper
 */


enum {
  VALUE_CHANGED,
  UNRETURNED_ERROR,
  ERROR,
  LAST_SIGNAL
};

static void gconf_client_class_init (GConfClientClass *klass);
static void gconf_client_init       (GConfClient      *client);
static void gconf_client_real_unreturned_error (GConfClient* client, GConfError* error);
static void gconf_client_real_error            (GConfClient* client, GConfError* error);
static void gconf_client_destroy               (GtkObject* object); 

static void gconf_client_cache                 (GConfClient* client,
                                                const gchar* key,
                                                GConfValue* value); /* takes ownership of value */

static gboolean gconf_client_lookup         (GConfClient* client,
                                             const gchar* key,
                                             GConfValue** valp);

static guint client_signals[LAST_SIGNAL] = { 0 };
static GtkObjectClass* parent_class = NULL;

GtkType
gconf_client_get_type (void)
{
  static GtkType client_type = 0;

  if (!client_type)
    {
      static const GtkTypeInfo client_info =
      {
        "GConfClient",
        sizeof (GConfClient),
        sizeof (GConfClientClass),
        (GtkClassInitFunc) gconf_client_class_init,
        (GtkObjectInitFunc) gconf_client_init,
        /* reserved_1 */ NULL,
        /* reserved_2 */ NULL,
        (GtkClassInitFunc) NULL,
      };

      client_type = gtk_type_unique (GTK_TYPE_OBJECT, &client_info);
    }

  return client_type;
}

static void
gconf_client_class_init (GConfClientClass *class)
{
  GtkObjectClass *object_class;

  object_class = (GtkObjectClass*) class;

  parent_class = gtk_type_class (gtk_object_get_type ());
  
  client_signals[VALUE_CHANGED] =
    gtk_signal_new ("value_changed",
                    GTK_RUN_LAST,
                    object_class->type,
                    GTK_SIGNAL_OFFSET (GConfClientClass, value_changed),
                    gtk_marshal_NONE__POINTER_POINTER,
                    GTK_TYPE_NONE, 2, GTK_TYPE_POINTER, GTK_TYPE_POINTER);
  
  client_signals[UNRETURNED_ERROR] =
    gtk_signal_new ("unreturned_error",
                    GTK_RUN_LAST,
                    object_class->type,
                    GTK_SIGNAL_OFFSET (GConfClientClass, unreturned_error),
                    gtk_marshal_NONE__POINTER,
                    GTK_TYPE_NONE, 1, GTK_TYPE_POINTER);

  client_signals[ERROR] =
    gtk_signal_new ("error",
                    GTK_RUN_LAST,
                    object_class->type,
                    GTK_SIGNAL_OFFSET (GConfClientClass, error),
                    gtk_marshal_NONE__POINTER,
                    GTK_TYPE_NONE, 1, GTK_TYPE_POINTER);
  
  gtk_object_class_add_signals (object_class, client_signals, LAST_SIGNAL);

  class->value_changed    = NULL;
  class->unreturned_error = gconf_client_real_unreturned_error;
  class->error            = gconf_client_real_error;

  object_class->destroy   = gconf_client_destroy;
}

static void
gconf_client_init (GConfClient *client)
{
  client->engine = NULL;
  client->error_mode = GCONF_CLIENT_HANDLE_NONE;
  client->cache_hash = g_hash_table_new(g_str_hash, g_str_equal);
  /* We create the listeners only if they're actually used */
  client->listeners = NULL;
}

static void
gconf_client_destroy               (GtkObject* object)
{
  GConfClient* client = GCONF_CLIENT(object);

  if (client->listeners != NULL)
    {
      gconf_listeners_destroy(client->listeners);
      client->listeners = NULL;
    }
  
  if (client->engine != NULL)
    {
      gconf_engine_unref(client->engine);
      client->engine = NULL;
    }

  if (client->cache_hash != NULL)
    {
      gconf_client_clear_cache(client);
      g_hash_table_destroy(client->cache_hash);
    }
  
  if (parent_class->destroy)
    (*parent_class->destroy)(object);
}

/*
 * Default error handlers
 */

static void
gconf_client_real_unreturned_error (GConfClient* client, GConfError* error)
{
  /* FIXME use dialogs in an idle function, depending on the error mode */

  g_warning("Default GConf error handler unimplemented, error is:\n   %s", error->str);
}

static void
gconf_client_real_error            (GConfClient* client, GConfError* error)
{
  /* FIXME use dialogs in an idle function, depending on the error mode */

  g_warning("Default GConf error handler unimplemented, error is:\n   %s", error->str);
}

/* Emit the proper signals for the error, and fill in err */
static gboolean
handle_error(GConfClient* client, GConfError* error, GConfError** err)
{
  if (error != NULL)
    {
      gconf_client_error(client, error);
      
      if (err == NULL)
        {
          gconf_client_unreturned_error(client, error);

          gconf_error_destroy(error);
        }
      else
        *err = error;

      return TRUE;
    }
  else
    return FALSE;
}

struct client_and_val {
  GConfClient* client;
  GConfValue* val;
};

static void
notify_listeners_callback(GConfListeners* listeners,
                          const gchar* key,
                          guint cnxn_id,
                          gpointer listener_data,
                          gpointer user_data)
{
  struct client_and_val* cav = user_data;
  Listener* l = listener_data;
  
  g_return_if_fail(cav != NULL);
  g_return_if_fail(cav->client != NULL);
  g_return_if_fail(GCONF_IS_CLIENT(cav->client));
  g_return_if_fail(l != NULL);
  g_return_if_fail(l->func != NULL);

  (*l->func)(cav->client, cnxn_id, key, cav->val, l->data);
}

static void
notify_from_server_callback(GConfEngine* conf, guint cnxn_id,
                            const gchar* key, GConfValue* value,
                            gpointer user_data)
{
  GConfClient* client = user_data;

  g_return_if_fail(client != NULL);
  g_return_if_fail(GCONF_IS_CLIENT(client));
  g_return_if_fail(client->engine == conf);

  /* First do the caching, so that state is sane for the
   * listeners or functions connected to value_changed.
   * We know this key is under a directory in our dir list.
   */
  gconf_client_cache(client,
                     key,
                     value ? gconf_value_copy(value) : NULL);

  /* Emit the value_changed signal before notifying specific listeners;
   * I'm not sure there's a reason this matters though
   */
  gconf_client_value_changed(client, key, value);

  /* Now notify our listeners, if any */
  if (client->listeners != NULL)
    {
      struct client_and_val cav;

      cav.client = client;
      cav.val = value;
      
      gconf_listeners_notify(client->listeners,
                             key,
                             notify_listeners_callback,
                             &cav);
    }
}

/*
 * Public API
 */

GConfClient*
gconf_client_new (void)
{
  GConfClient *client;

  g_return_val_if_fail(gconf_is_initialized(), NULL);
  
  client = gtk_type_new (gconf_client_get_type ());

  client->engine = gconf_engine_new();
  
  return client;
}

GConfClient*
gconf_client_new_with_engine (GConfEngine* engine)
{
  GConfClient *client;

  g_return_val_if_fail(gconf_is_initialized(), NULL);
  
  client = gtk_type_new (gconf_client_get_type ());

  /* take over a ref count */
  client->engine = engine;
  
  return client;
}

void
gconf_client_add_dir     (GConfClient* client,
                          const gchar* dirname,
                          GConfClientPreloadType preload,
                          GConfError** err)
{
  Dir* d;
  guint notify_id = 0;
  GConfError* error = NULL;
  
#ifndef G_DISABLE_CHECKS
  {
    GSList* tmp;
    
    tmp = client->dir_list;

    while (tmp != NULL)
      {
        Dir* old = tmp->data;
        
        /* Disallow overlap */
        g_return_if_fail(!gconf_key_is_below(old->name, dirname));
        g_return_if_fail(!gconf_key_is_below(dirname, old->name));
        
        tmp = g_slist_next(tmp);
      }
  }
#endif

  notify_id = gconf_notify_add(client->engine,
                               dirname,
                               notify_from_server_callback,
                               client,
                               &error);

  /* We got a notify ID or we got an error, not both */
  g_return_if_fail( (notify_id != 0 && error == NULL) ||
                    (notify_id == 0 && error != NULL) );
  
  if (handle_error(client, error, err))
    return;
  
  d = dir_new(dirname, notify_id);
  
  client->dir_list = g_slist_prepend(client->dir_list, d);

  g_assert(error == NULL);
  
  gconf_client_preload(client, dirname, preload, &error);

  handle_error(client, error, err);
}

void
gconf_client_remove_dir  (GConfClient* client,
                          const gchar* dirname)
{
  Dir* found = NULL;
  GSList* tmp;

  /* remove dir from list */
  tmp = client->dir_list;

  while (tmp != NULL)
    {
      Dir* d = tmp->data;

      if (strcmp(d->name, dirname) == 0)
        {
          found = d;
          break;
        }

      tmp = g_slist_next(tmp);
    }

  if (found != NULL)
    {
      /* not totally efficient */
      client->dir_list = g_slist_remove(client->dir_list, found);
        
      /* remove notify for this dir */
      
      gconf_notify_remove(client->engine, found->notify_id);
      found->notify_id = 0;
      
      dir_destroy(found);
    }
#ifndef G_DISABLE_CHECKS
  else
    g_warning("Directory `%s' was not being monitored by GConfClient %p",
              dirname, client);
#endif
}


guint
gconf_client_notify_add(GConfClient* client,
                        const gchar* namespace_section,
                        GConfClientNotifyFunc func,
                        gpointer user_data,
                        GFreeFunc destroy_notify,
                        GConfError** err)
{
  guint cnxn_id = 0;
  
  g_return_val_if_fail(client != NULL, 0);
  g_return_val_if_fail(GCONF_IS_CLIENT(client), 0);

  if (client->listeners == NULL)
    client->listeners = gconf_listeners_new();
  
  cnxn_id = gconf_listeners_add(client->listeners,
                                namespace_section,
                                listener_new(func, destroy_notify, user_data),
                                (GFreeFunc)listener_destroy);

  return cnxn_id;
}

void
gconf_client_notify_remove  (GConfClient* client,
                             guint cnxn)
{
  g_return_if_fail(client != NULL);
  g_return_if_fail(GCONF_IS_CLIENT(client));
  g_return_if_fail(client->listeners != NULL);
  
  gconf_listeners_remove(client->listeners, cnxn);
  
  if (gconf_listeners_count(client->listeners) == 0)
    {
      gconf_listeners_destroy(client->listeners);
      client->listeners = NULL;
    }
}

void
gconf_client_set_error_handling(GConfClient* client,
                                GConfClientErrorHandlingMode mode,
                                /* func can be NULL for none or N/A */
                                GConfClientParentWindowFunc func,
                                gpointer user_data)
{
  g_return_if_fail(client != NULL);
  g_return_if_fail(GCONF_IS_CLIENT(client));

  client->error_mode = mode;
  client->parent_func = func;
  client->parent_user_data = user_data;
}

static gboolean
clear_cache_foreach(gchar* key, CacheEntry* ce, GConfClient* client)
{
  g_free(key);
  cache_entry_destroy(ce);

  return TRUE;
}

void
gconf_client_clear_cache(GConfClient* client)
{
  g_return_if_fail(client != NULL);
  g_return_if_fail(GCONF_IS_CLIENT(client));
  
  g_hash_table_foreach_remove(client->cache_hash, (GHRFunc)clear_cache_foreach,
                              client);

  g_assert(g_hash_table_size(client->cache_hash) == 0);
}

void
gconf_client_preload    (GConfClient* client,
                         const gchar* dirname,
                         GConfClientPreloadType type,
                         GConfError** err)
{

  /* FIXME */
  
  /* Include a check that the dirname is in dir_list */


  
  switch (type)
    {
    case GCONF_CLIENT_PRELOAD_NONE:
      /* nothing */
      break;

    case GCONF_CLIENT_PRELOAD_ONELEVEL:
      /* FIXME */
      break;

    case GCONF_CLIENT_PRELOAD_RECURSIVE:
      /* FIXME */
      break;

    default:
      g_assert_not_reached();
      break;
    }
}

/*
 * Basic key-manipulation facilities
 */

void
gconf_client_set             (GConfClient* client,
                              const gchar* key,
                              GConfValue* val,
                              GConfError** err)
{
  GConfError* error = NULL;
  
  gconf_set(client->engine, key, val, &error);

  handle_error(client, error, err);
}

GConfValue*
gconf_client_get             (GConfClient* client,
                              const gchar* key,
                              GConfError** err)
{
  GConfError* error = NULL;
  GConfValue* val = NULL;

  if (gconf_client_lookup(client, key, &val))
    return val; /* val may be NULL for unset */

  /* If val was set then it must have been in the cache.
   * If we got here then val wasn't in the cache.
   */
  g_assert(val == NULL);
  
  val = gconf_get(client->engine, key, &error);
      
  handle_error(client, error, err);
      
  return val;
}

gboolean
gconf_client_unset          (GConfClient* client,
                             const gchar* key, GConfError** err)
{
  GConfError* error = NULL;
  
  gconf_unset(client->engine, key, &error);

  handle_error(client, error, err);

  if (error != NULL)
    return FALSE;
  else
    return TRUE;
}

GSList*
gconf_client_all_entries    (GConfClient* client,
                             const gchar* dir, GConfError** err)
{
  GConfError* error = NULL;
  GSList* retval;
  
  retval = gconf_all_entries(client->engine, dir, &error);

  handle_error(client, error, err);

  return retval;
}

GSList*
gconf_client_all_dirs       (GConfClient* client,
                             const gchar* dir, GConfError** err)
{
  GConfError* error = NULL;
  GSList* retval;
  
  retval = gconf_all_dirs(client->engine, dir, &error);

  handle_error(client, error, err);

  return retval;
}

void
gconf_client_suggest_sync   (GConfClient* client,
                             GConfError** err)
{
  GConfError* error = NULL;
  
  gconf_suggest_sync(client->engine, &error);

  handle_error(client, error, err);
}

gboolean
gconf_client_dir_exists     (GConfClient* client,
                             const gchar* dir, GConfError** err)
{
  GConfError* error = NULL;
  gboolean retval;
  
  retval = gconf_dir_exists(client->engine, dir, &error);

  handle_error(client, error, err);

  return retval;
}

static gboolean
check_type(GConfValue* val, GConfValueType t, GConfError** err)
{
  if (val->type != t)
    {
      gconf_set_error(err, GCONF_TYPE_MISMATCH,
                      _("Expected `%s' got `%s'"),
                      gconf_value_type_to_string(t),
                      gconf_value_type_to_string(val->type));
      return FALSE;
    }
  else
    return TRUE;
}

static GConfValue*
get(GConfClient* client, const gchar* key, GConfError** error)
{
  GConfValue* val = NULL;

  g_return_val_if_fail(client != NULL, NULL);
  g_return_val_if_fail(GCONF_IS_CLIENT(client), NULL);
  g_return_val_if_fail(error != NULL, NULL);
  g_return_val_if_fail(*error == NULL, NULL);
  
  /* Check our client-side cache */
  if (gconf_client_lookup(client, key, &val))
    return gconf_value_copy(val); /* stored in cache, not necessarily set though */

  g_assert(val == NULL); /* if it was in the cache we should have returned */

  /* Check the GConfEngine */
  val = gconf_get(client->engine, key, error);

  if (*error != NULL)
    {
      g_return_val_if_fail(val == NULL, NULL);
      return NULL;
    }
  else
    {
      /* Cache this value, if it's in our directory list. FIXME could
         speed this up. */
      GSList* tmp;

      tmp = client->dir_list;
      while (tmp != NULL)
        {
          Dir* d = tmp->data;

          if (gconf_key_is_below(d->name, key))
            {
              /* note that we cache a _copy_ */
              gconf_client_cache(client, key,
                                 val ? gconf_value_copy(val) : NULL);
              break;
            }
          else
            tmp = g_slist_next(tmp);
        }

      return val;
    }
}

gdouble
gconf_client_get_float (GConfClient* client, const gchar* key,
                        gdouble def, GConfError** err)
{
  GConfError* error = NULL;
  GConfValue* val;

  g_return_val_if_fail(err == NULL || *err == NULL, 0.0);
  
  val = get(client, key, &error);

  if (val != NULL)
    {
      gdouble retval = def;

      g_assert(error == NULL);
      
      if (check_type(val, GCONF_VALUE_FLOAT, &error))
        retval = gconf_value_float(val);
      else
        handle_error(client, error, err);

      gconf_value_destroy(val);

      return retval;
    }
  else
    {
      if (error != NULL)
        handle_error(client, error, err);
      return def;
    }
}

gint
gconf_client_get_int   (GConfClient* client, const gchar* key,
                        gint def, GConfError** err)
{
  GConfError* error = NULL;
  GConfValue* val;

  g_return_val_if_fail(err == NULL || *err == NULL, 0);
  
  val = get(client, key, &error);

  if (val != NULL)
    {
      gint retval = def;

      g_assert(error == NULL);
      
      if (check_type(val, GCONF_VALUE_INT, &error))
        retval = gconf_value_int(val);
      else
        handle_error(client, error, err);

      gconf_value_destroy(val);

      return retval;
    }
  else
    {
      if (error != NULL)
        handle_error(client, error, err);
      return def;
    }
}

gchar*
gconf_client_get_string(GConfClient* client, const gchar* key,
                        const gchar* def, 
                        GConfError** err)
{
  GConfError* error = NULL;
  GConfValue* val;

  g_return_val_if_fail(err == NULL || *err == NULL, NULL);
  
  val = get(client, key, &error);

  if (val != NULL)
    {
      gchar* retval = NULL;

      g_assert(error == NULL);
      
      if (check_type(val, GCONF_VALUE_STRING, &error))
        retval = gconf_value_string(val);
      else
        handle_error(client, error, err);

      /* This is a cheat; don't copy */
      if (retval != NULL)
        val->d.string_data = NULL; /* don't delete the string we are returning */
      else
        retval = def ? g_strdup(def) : NULL;
      
      gconf_value_destroy(val);

      return retval;
    }
  else
    {
      if (error != NULL)
        handle_error(client, error, err);
      return def ? g_strdup(def) : NULL;
    }
}


gboolean
gconf_client_get_bool  (GConfClient* client, const gchar* key,
                        gboolean def, GConfError** err)
{
  GConfError* error = NULL;
  GConfValue* val;

  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);
  
  val = get(client, key, &error);

  if (val != NULL)
    {
      gboolean retval = def;

      g_assert(error == NULL);
      
      if (check_type(val, GCONF_VALUE_BOOL, &error))
        retval = gconf_value_bool(val);
      else
        handle_error(client, error, err);

      gconf_value_destroy(val);

      return retval;
    }
  else
    {
      if (error != NULL)
        handle_error(client, error, err);
      return def;
    }
}

GConfSchema*
gconf_client_get_schema  (GConfClient* client,
                          const gchar* key, GConfError** err)
{
  GConfError* error = NULL;
  GConfValue* val;

  g_return_val_if_fail(err == NULL || *err == NULL, NULL);
  
  val = get(client, key, &error);

  if (val != NULL)
    {
      GConfSchema* retval = NULL;

      g_assert(error == NULL);
      
      if (check_type(val, GCONF_VALUE_SCHEMA, &error))
        retval = gconf_value_schema(val);
      else
        handle_error(client, error, err);

      /* This is a cheat; don't copy */
      if (retval != NULL)
        val->d.schema_data = NULL; /* don't delete the schema */
      
      gconf_value_destroy(val);

      return retval;
    }
  else
    {
      if (error != NULL)
        handle_error(client, error, err);
      return NULL;
    }
}

GSList*
gconf_client_get_list    (GConfClient* client, const gchar* key,
                          GConfValueType list_type, GConfError** err)
{
  GConfError* error = NULL;
  GConfValue* val;

  g_return_val_if_fail(err == NULL || *err == NULL, NULL);
  
  val = get(client, key, &error);

  if (val != NULL)
    {
      GSList* retval;

      g_assert(error == NULL);

      /* This function checks the type and destroys "val" */
      retval = gconf_value_list_to_primitive_list_destructive(val, list_type, &error);

      if (error != NULL)
        {
          g_assert(retval == NULL);
          handle_error(client, error, err);
          return NULL;
        }
      else
        return retval;
    }
  else
    {
      if (error != NULL)
        handle_error(client, error, err);
      return NULL;
    }
}

gboolean
gconf_client_get_pair    (GConfClient* client, const gchar* key,
                          GConfValueType car_type, GConfValueType cdr_type,
                          gpointer car_retloc, gpointer cdr_retloc,
                          GConfError** err)
{
  GConfError* error = NULL;
  GConfValue* val;

  g_return_val_if_fail(err == NULL || *err == NULL, NULL);
  
  val = get(client, key, &error);

  if (val != NULL)
    {
      g_assert(error == NULL);

      /* This function checks the type and destroys "val" */
      if (gconf_value_pair_to_primitive_pair_destructive(val, car_type, cdr_type,
                                                         car_retloc, cdr_retloc,
                                                         &error))
        {
          g_assert(error == NULL);
          return TRUE;
        }
      else
        {
          g_assert(error != NULL);
          handle_error(client, error, err);
          return FALSE;
        }
    }
  else
    {
      if (error != NULL)
        {
          handle_error(client, error, err);
          return FALSE;
        }
      else
        return TRUE;
    }

}


/*
 * For the set functions, we just set normally, and wait for the
 * notification to come back from the server before we update
 * our cache. This may be the wrong thing; maybe we should
 * update immediately?
 * Problem with delayed update: user calls set() then get(),
 *  results in weirdness
 * Problem with with regular update: get() before the notify
 *  is out of sync with the listening parts of the application
 * 
 * It is somewhat academic now anyway because the _set() call
 * won't return until all the notifications have happened, so the
 * notify signal will be emitted inside the set() call.
 */

gboolean
gconf_client_set_float   (GConfClient* client, const gchar* key,
                          gdouble val, GConfError** err)
{
  GConfError* error = NULL;
  
  g_return_val_if_fail(client != NULL, FALSE);
  g_return_val_if_fail(GCONF_IS_CLIENT(client), FALSE);  
  g_return_val_if_fail(key != NULL, FALSE);
  
  if (gconf_set_float(client->engine, key, val, &error))
    return TRUE;
  else
    {
      handle_error(client, error, err);
      return FALSE;
    }
}

gboolean
gconf_client_set_int     (GConfClient* client, const gchar* key,
                          gint val, GConfError** err)
{
  GConfError* error = NULL;
  
  g_return_val_if_fail(client != NULL, FALSE);
  g_return_val_if_fail(GCONF_IS_CLIENT(client), FALSE);  
  g_return_val_if_fail(key != NULL, FALSE);
  
  if (gconf_set_int(client->engine, key, val, &error))
    return TRUE;
  else
    {
      handle_error(client, error, err);
      return FALSE;
    }
}

gboolean
gconf_client_set_string  (GConfClient* client, const gchar* key,
                          const gchar* val, GConfError** err)
{
  GConfError* error = NULL;
  
  g_return_val_if_fail(client != NULL, FALSE);
  g_return_val_if_fail(GCONF_IS_CLIENT(client), FALSE);  
  g_return_val_if_fail(key != NULL, FALSE);
  g_return_val_if_fail(val != NULL, FALSE);
  
  if (gconf_set_string(client->engine, key, val, &error))
    return TRUE;
  else
    {
      handle_error(client, error, err);
      return FALSE;
    }
}

gboolean
gconf_client_set_bool    (GConfClient* client, const gchar* key,
                          gboolean val, GConfError** err)
{
  GConfError* error = NULL;
  
  g_return_val_if_fail(client != NULL, FALSE);
  g_return_val_if_fail(GCONF_IS_CLIENT(client), FALSE);  
  g_return_val_if_fail(key != NULL, FALSE);
  
  if (gconf_set_bool(client->engine, key, val, &error))
    return TRUE;
  else
    {
      handle_error(client, error, err);
      return FALSE;
    }
}

gboolean
gconf_client_set_schema  (GConfClient* client, const gchar* key,
                          GConfSchema* val, GConfError** err)
{
  GConfError* error = NULL;
  
  g_return_val_if_fail(client != NULL, FALSE);
  g_return_val_if_fail(GCONF_IS_CLIENT(client), FALSE);  
  g_return_val_if_fail(key != NULL, FALSE);
  g_return_val_if_fail(val != NULL, FALSE);
  
  if (gconf_set_schema(client->engine, key, val, &error))
    return TRUE;
  else
    {
      handle_error(client, error, err);
      return FALSE;
    }
}

gboolean
gconf_client_set_list    (GConfClient* client, const gchar* key,
                          GConfValueType list_type,
                          GSList* list,
                          GConfError** err)
{
  GConfError* error = NULL;
  
  g_return_val_if_fail(client != NULL, FALSE);
  g_return_val_if_fail(GCONF_IS_CLIENT(client), FALSE);  
  g_return_val_if_fail(key != NULL, FALSE);
  
  if (gconf_set_list(client->engine, key, list_type, list, &error))
    return TRUE;
  else
    {
      handle_error(client, error, err);
      return FALSE;
    }
}

gboolean
gconf_client_set_pair    (GConfClient* client, const gchar* key,
                          GConfValueType car_type, GConfValueType cdr_type,
                          gconstpointer address_of_car,
                          gconstpointer address_of_cdr,
                          GConfError** err)
{
  GConfError* error = NULL;
  
  g_return_val_if_fail(client != NULL, FALSE);
  g_return_val_if_fail(GCONF_IS_CLIENT(client), FALSE);  
  g_return_val_if_fail(key != NULL, FALSE);
  
  if (gconf_set_pair(client->engine, key, car_type, cdr_type,
                     address_of_car, address_of_car, &error))
    return TRUE;
  else
    {
      handle_error(client, error, err);
      return FALSE;
    }
}


/*
 * Functions to emit signals
 */

void
gconf_client_error                  (GConfClient* client, GConfError* error)
{
  g_return_if_fail(client != NULL);
  g_return_if_fail(GCONF_IS_CLIENT(client));
  
  gtk_signal_emit(GTK_OBJECT(client), client_signals[ERROR], error);
}

void
gconf_client_unreturned_error       (GConfClient* client, GConfError* error)
{
  g_return_if_fail(client != NULL);
  g_return_if_fail(GCONF_IS_CLIENT(client));

  gtk_signal_emit(GTK_OBJECT(client), client_signals[UNRETURNED_ERROR], error);
}

void
gconf_client_value_changed          (GConfClient* client,
                                     const gchar* key,
                                     GConfValue* value)
{
  g_return_if_fail(client != NULL);
  g_return_if_fail(GCONF_IS_CLIENT(client));
  g_return_if_fail(key != NULL);
  
  gtk_signal_emit(GTK_OBJECT(client), client_signals[VALUE_CHANGED],
                  key, value);
}

/*
 * Internal utility
 */

static void
gconf_client_cache (GConfClient* client,
                    const gchar* key,
                    GConfValue* value)
{
  /* Remember: value may be NULL */
  
  gpointer oldkey = NULL, oldval = NULL;

  if (g_hash_table_lookup_extended(client->cache_hash, key, &oldkey, &oldval))
    {
      /* Already have a value, update it */
      CacheEntry* ce = oldval;

      g_assert(ce != NULL);

      if (ce->value != NULL)
        gconf_value_destroy(ce->value);

      ce->value = value;
    }
  else
    {
      /* Create a new entry */
      CacheEntry* ce = cache_entry_new(value);
      g_hash_table_insert(client->cache_hash, g_strdup(key), ce);
    }
}

static gboolean
gconf_client_lookup         (GConfClient* client,
                             const gchar* key,
                             GConfValue** valp)
{
  CacheEntry* ce;

  g_return_val_if_fail(valp != NULL, FALSE);
  g_return_val_if_fail(*valp == NULL, FALSE);
  
  ce = g_hash_table_lookup(client->cache_hash, key);

  if (ce != NULL)
    {
      *valp = ce->value;
      return TRUE;
    }
  else
    return FALSE;
}


/*
 * CacheEntry
 */ 

static CacheEntry*
cache_entry_new(GConfValue* val)
{
  CacheEntry* ce;

  ce = g_new(CacheEntry, 1);

  /* val may be NULL */
  ce->value = val;

  return ce;
}

static void
cache_entry_destroy(CacheEntry* ce)
{
  g_return_if_fail(ce != NULL);
  
  if (ce->value != NULL)
    gconf_value_destroy(ce->value);

  g_free(ce);
}

/*
 * Dir
 */

static Dir*
dir_new(const gchar* name, guint notify_id)
{
  Dir* d;

  d = g_new(Dir, 1);

  d->name = g_strdup(name);
  d->notify_id = notify_id;

  return d;
}

static void
dir_destroy(Dir* d)
{
  g_return_if_fail(d != NULL);
  g_return_if_fail(d->notify_id == 0);
  
  g_free(d->name);
  g_free(d);
}

/*
 * Listener
 */

static Listener* 
listener_new(GConfClientNotifyFunc func,
             GFreeFunc destroy_notify,
             gpointer data)
{
  Listener* l;

  l = g_new(Listener, 1);

  l->func = func;
  l->data = data;
  l->destroy_notify = destroy_notify;
  
  return l;
}

static void
listener_destroy(Listener* l)
{
  g_return_if_fail(l != NULL);

  if (l->destroy_notify)
    (* l->destroy_notify) (l->data);
  
  g_free(l);
}
