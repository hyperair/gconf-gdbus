/* -*- tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* GConf
 * Copyright (C) 1999, 2000, 2000 Red Hat Inc.
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

#include <stdio.h>
#include "gconf-client.h"
#include <gtk/gtksignal.h>
#include <gtk/gtktypeutils.h>
#include <gconf/gconf-internals.h>

/*
 * Error handler override
 */

static GConfClientErrorHandlerFunc global_error_handler = NULL;

void
gconf_client_set_global_default_error_handler(GConfClientErrorHandlerFunc func)
{
  global_error_handler = func;
}

/*
 * CacheEntry
 */ 

typedef struct _CacheEntry CacheEntry;

struct _CacheEntry {
  GConfValue* value;
  /* Whether "value" was a default from a schema; i.e.
     if this is TRUE, then value wasn't set, we just used
     a default. */
  guint is_default : 1;
  guint is_writable : 1;
};

static CacheEntry* cache_entry_new(GConfValue* val,
                                   gboolean is_default,
                                   gboolean is_writable);
static void        cache_entry_destroy(CacheEntry* ce);

/*
 * Dir object (for list of directories we're watching)
 */

typedef struct _Dir Dir;

struct _Dir {
  gchar* name;
  guint notify_id;
  /* number of times this dir has been added */
  guint add_count;
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

static void         register_client   (GConfClient *client);
static GConfClient *lookup_client     (GConfEngine *engine);
static void         unregister_client (GConfClient *client);

static void gconf_client_class_init (GConfClientClass *klass);
static void gconf_client_init       (GConfClient      *client);
static void gconf_client_real_unreturned_error (GConfClient* client, GError* error);
static void gconf_client_real_error            (GConfClient* client, GError* error);
static void gconf_client_finalize              (GtkObject* object); 

static void gconf_client_cache                 (GConfClient* client,
                                                const gchar* key,
                                                gboolean is_default,
                                                gboolean is_writable,
                                                GConfValue* value); /* takes ownership of value */

static gboolean gconf_client_lookup         (GConfClient* client,
                                             const gchar* key,
                                             gboolean use_default,
                                             gboolean* is_default,
                                             gboolean* is_writable,
                                             GConfValue** valp);

static void gconf_client_real_remove_dir    (GConfClient* client,
                                             Dir* d,
					     GError** err);

static GConfValue* get (GConfClient  *client,
                        const gchar  *key,
                        gboolean      use_default,
                        gboolean     *is_default_retloc,
                        gboolean     *is_writable_retloc,
                        GError      **error);


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
  
  object_class->finalize  = gconf_client_finalize;
}

static void
gconf_client_init (GConfClient *client)
{
  client->engine = NULL;
  client->error_mode = GCONF_CLIENT_HANDLE_UNRETURNED;
  client->dir_hash = g_hash_table_new(g_str_hash, g_str_equal);
  client->cache_hash = g_hash_table_new(g_str_hash, g_str_equal);
  /* We create the listeners only if they're actually used */
  client->listeners = NULL;
}

static gboolean
destroy_dir_foreach_remove(gpointer key, gpointer value, gpointer user_data)
{
  GConfClient *client = user_data;
  Dir* d = value;
  
  /* remove notify for this dir */
  
  if(d->notify_id != 0)
	  gconf_engine_notify_remove(client->engine, d->notify_id);
  d->notify_id = 0;

  dir_destroy(value);

  return TRUE;
}

static void
gconf_client_finalize (GtkObject* object)
{
  GConfClient* client = GCONF_CLIENT(object);

  g_hash_table_foreach_remove(client->dir_hash,
                              destroy_dir_foreach_remove, client);
  
  gconf_client_clear_cache(client);
  
  if (client->listeners != NULL)
    {
      gconf_listeners_free(client->listeners);
      client->listeners = NULL;
    }

  g_hash_table_destroy(client->dir_hash);
  client->dir_hash = NULL;
  
  g_hash_table_destroy(client->cache_hash);
  client->cache_hash = NULL;

  unregister_client (client);
  
  if (client->engine != NULL)
    {
      gconf_engine_unref(client->engine);
      client->engine = NULL;
    }

  if (parent_class->finalize)
    (*parent_class->finalize)(object);
}

/*
 * Default error handlers
 */

static void
gconf_client_real_unreturned_error (GConfClient* client, GError* error)
{
  if (client->error_mode == GCONF_CLIENT_HANDLE_UNRETURNED)
    {
      if (global_error_handler != NULL)
        {
          (*global_error_handler) (client, error);
        }
      else
        {
          fprintf (stderr, _("GConf Error: %s\n"),
                   error->message);
        }
    }
}

static void
gconf_client_real_error            (GConfClient* client, GError* error)
{
  if (client->error_mode == GCONF_CLIENT_HANDLE_ALL)
    {
      if (global_error_handler != NULL)
        {
          (*global_error_handler) (client, error);
        }
      else
        {
          fprintf (stderr, _("GConf Error: %s\n"),
                   error->message);
        }
    }
}

/* Emit the proper signals for the error, and fill in err */
static gboolean
handle_error(GConfClient* client, GError* error, GError** err)
{
  if (error != NULL)
    {
      gconf_client_error(client, error);
      
      if (err == NULL)
        {
          gconf_client_unreturned_error(client, error);

          g_error_free(error);
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
  GConfEntry* entry;
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

  (*l->func)(cav->client, cnxn_id, cav->entry, l->data);
}

static void
notify_from_server_callback(GConfEngine* conf, guint cnxn_id,
                            GConfEntry *entry,
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
                     entry->key,
                     entry->is_default,
                     entry->is_writable,
                     entry->value ? gconf_value_copy(entry->value) : NULL);

  /* Emit the value_changed signal before notifying specific listeners;
   * I'm not sure there's a reason this matters though
   */
  gconf_client_value_changed(client,
                             entry->key,
                             entry->value);

  /* Now notify our listeners, if any */
  if (client->listeners != NULL)
    {
      struct client_and_val cav;

      cav.client = client;
      cav.entry = entry;
      
      gconf_listeners_notify(client->listeners,
                             entry->key,
                             notify_listeners_callback,
                             &cav);
    }
}

/*
 * Public API
 */

GConfClient*
gconf_client_get_default (void)
{
  GConfClient *client;
  GConfEngine *engine;
  
  g_return_val_if_fail(gconf_is_initialized(), NULL);

  engine = gconf_engine_get_default ();
  
  client = lookup_client (engine);
  if (client)
    {
      g_assert (client->engine == engine);
      gtk_object_ref (GTK_OBJECT (client));
      gconf_engine_unref (engine);
      return client;
    }
  else
    {
      client = gtk_type_new (gconf_client_get_type ());

      /* Emulate GObject */
      gtk_object_ref (GTK_OBJECT (client));
      gtk_object_sink (GTK_OBJECT (client));

      client->engine = engine;
      register_client (client);
    }
  
  return client;
}

GConfClient*
gconf_client_get_for_engine (GConfEngine* engine)
{
  GConfClient *client;

  g_return_val_if_fail(gconf_is_initialized(), NULL);

  client = lookup_client (engine);
  if (client)
    {
      g_assert (client->engine == engine);
      gtk_object_ref (GTK_OBJECT (client));
      return client;
    }
  else
    {
      client = gtk_type_new (gconf_client_get_type ());

      /* Emulate GObject */
      gtk_object_ref (GTK_OBJECT (client));
      gtk_object_sink (GTK_OBJECT (client));
      
      client->engine = engine;

      gconf_engine_ref(client->engine);

      register_client (client);
    }
  
  return client;
}

typedef struct {
  GConfClient *client;
  Dir *lower_dir;
  const char *dirname;
} OverlapData;

static void
foreach_setup_overlap(gpointer key, gpointer value, gpointer user_data)
{
  GConfClient *client;
  Dir *dir = value;
  OverlapData * od = user_data;

  client = od->client;

  /* if we have found the first (well there is only one anyway) directory
   * that includes us that has a notify handler
   */
#ifdef GCONF_ENABLE_DEBUG
  if (dir->notify_id != 0 &&
      gconf_key_is_below(dir->name, od->dirname))
    {
      g_assert(od->lower_dir == NULL);
      od->lower_dir = dir;
    }
#else
  if (od->lower_dir == NULL &&
      dir->notify_id != 0 &&
      gconf_key_is_below(dir->name, od->dirname))
      od->lower_dir = dir;
#endif
  /* if we have found a directory that we include and it has
   * a notify_id, remove the notify handler now
   * FIXME: this is a race, from now on we can miss notifies, it is
   * not an incredible amount of time so this is not a showstopper */
  else if (dir->notify_id != 0 &&
           gconf_key_is_below (od->dirname, dir->name))
    {
      gconf_engine_notify_remove (client->engine, dir->notify_id);
      dir->notify_id = 0;
    }
}

static Dir *
setup_overlaps(GConfClient* client, const gchar* dirname)
{
  OverlapData od;

  od.client = client;
  od.lower_dir = NULL;
  od.dirname = dirname;

  g_hash_table_foreach(client->dir_hash, foreach_setup_overlap, &od);

  return od.lower_dir;
}

void
gconf_client_add_dir     (GConfClient* client,
                          const gchar* dirname,
                          GConfClientPreloadType preload,
                          GError** err)
{
  Dir* d;
  guint notify_id = 0;
  GError* error = NULL;

  g_return_if_fail(gconf_valid_key(dirname, NULL));
  
  d = g_hash_table_lookup (client->dir_hash, dirname);

  if (d == NULL)
    {
      Dir *overlap_dir;

      overlap_dir = setup_overlaps (client, dirname);

      /* only if there is no directory that includes us
       * already add a notify
       */
      if (overlap_dir == NULL)
        {

          notify_id = gconf_engine_notify_add (client->engine,
                                               dirname,
                                               notify_from_server_callback,
                                               client,
                                               &error);
      
          /* We got a notify ID or we got an error, not both */
          g_return_if_fail( (notify_id != 0 && error == NULL) ||
                            (notify_id == 0 && error != NULL) );
      
      
          if (handle_error(client, error, err))
            return;

          g_assert(error == NULL);
        }
      else
        {
          notify_id = 0;
        }
      
      d = dir_new(dirname, notify_id);

      g_hash_table_insert(client->dir_hash, d->name, d);

      gconf_client_preload(client, dirname, preload, &error);

      handle_error(client, error, err);
    }

  g_assert(d != NULL);

  d->add_count += 1;
}

typedef struct {
  GConfClient *client;
  GError *error;
} AddNotifiesData;

static void
foreach_add_notifies(gpointer key, gpointer value, gpointer user_data)
{
  AddNotifiesData *ad = user_data;
  GConfClient *client;
  Dir *dir = value;

  client = ad->client;

  if (ad->error != NULL)
    return;

  if (dir->notify_id == 0)
    {
      Dir *overlap_dir;
      overlap_dir = setup_overlaps(client, dir->name);

      /* only if there is no directory that includes us
       * already add a notify */
      if (overlap_dir == NULL)
        {
          dir->notify_id = gconf_engine_notify_add(client->engine,
					    dir->name,
					    notify_from_server_callback,
					    client,
					    &ad->error);

          /* We got a notify ID or we got an error, not both */
          g_return_if_fail( (dir->notify_id != 0 && ad->error == NULL) ||
		            (dir->notify_id == 0 && ad->error != NULL) );

          /* if error is returned, then we'll just ignore
           * things until the end */
        }
    }
}

static void
gconf_client_real_remove_dir    (GConfClient* client,
                                 Dir* d,
                                 GError** err)
{
  AddNotifiesData ad;

  g_return_if_fail(d != NULL);
  g_return_if_fail(d->add_count == 0);
  
  g_hash_table_remove(client->dir_hash, d->name);
  
  /* remove notify for this dir */
  
  if (d->notify_id != 0)
    gconf_engine_notify_remove(client->engine, d->notify_id);
  d->notify_id = 0;
  
  dir_destroy(d);

  ad.client = client;
  ad.error = NULL;

  g_hash_table_foreach(client->dir_hash, foreach_add_notifies, &ad);

  handle_error(client, ad.error, err);
}

void
gconf_client_remove_dir  (GConfClient* client,
                          const gchar* dirname,
                          GError** err)
{
  Dir* found = NULL;

  found = g_hash_table_lookup(client->dir_hash,
                              dirname);
  
  if (found != NULL)
    {
      g_return_if_fail(found->add_count > 0);

      found->add_count -= 1;

      if (found->add_count == 0) 
        gconf_client_real_remove_dir(client, found, err);
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
                        GError** err)
{
  guint cnxn_id = 0;
  
  g_return_val_if_fail(client != NULL, 0);
  g_return_val_if_fail(GCONF_IS_CLIENT(client), 0);

  if (client->listeners == NULL)
    client->listeners = gconf_listeners_new();
  
  cnxn_id = gconf_listeners_add (client->listeners,
                                 namespace_section,
                                 listener_new (func, destroy_notify, user_data),
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
      gconf_listeners_free(client->listeners);
      client->listeners = NULL;
    }
}

void
gconf_client_set_error_handling(GConfClient* client,
                                GConfClientErrorHandlingMode mode)
{
  g_return_if_fail(client != NULL);
  g_return_if_fail(GCONF_IS_CLIENT(client));

  client->error_mode = mode;
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

static void
cache_pairs_in_dir(GConfClient* client, const gchar* path);

static void 
recurse_subdir_list(GConfClient* client, GSList* subdirs, const gchar* parent)
{
  GSList* tmp;

  tmp = subdirs;
  
  while (tmp != NULL)
    {
      gchar* s = tmp->data;
      gchar* full = gconf_concat_dir_and_key(parent, s);
      
      cache_pairs_in_dir(client, full);

      recurse_subdir_list(client, gconf_engine_all_dirs(client->engine, full, NULL), full);

      g_free(s);
      g_free(full);
      
      tmp = g_slist_next(tmp);
    }
  
  g_slist_free(subdirs);
}

static void 
cache_pairs_in_dir(GConfClient* client, const gchar* dir)
{
  GSList* pairs;
  GSList* tmp;
  GError* error = NULL;

  pairs = gconf_engine_all_entries(client->engine, dir, &error);
          
  if (error != NULL)
    {
      fprintf(stderr, _("GConf warning: failure listing pairs in `%s': %s"),
              dir, error->message);
      g_error_free(error);
      error = NULL;
    }

  if (pairs != NULL)
    {
      tmp = pairs;

      while (tmp != NULL)
        {
          GConfEntry* pair = tmp->data;
          
          gconf_client_cache(client,
                             gconf_entry_get_key (pair),
                             gconf_entry_get_is_default(pair),
                             gconf_entry_get_is_writable(pair),
                             gconf_entry_steal_value(pair));

          gconf_entry_free(pair);

          tmp = g_slist_next(tmp);
        }

      g_slist_free(pairs);
    }
}

void
gconf_client_preload    (GConfClient* client,
                         const gchar* dirname,
                         GConfClientPreloadType type,
                         GError** err)
{

  g_return_if_fail(client != NULL);
  g_return_if_fail(GCONF_IS_CLIENT(client));
  g_return_if_fail(dirname != NULL);

#ifdef GCONF_ENABLE_DEBUG
  if (g_hash_table_lookup(client->dir_hash, dirname) == NULL)
    {
      g_warning("Can only preload directories you've added with gconf_client_add_dir()");
      return;
    }
#endif
  
  switch (type)
    {
    case GCONF_CLIENT_PRELOAD_NONE:
      /* nothing */
      break;

    case GCONF_CLIENT_PRELOAD_ONELEVEL:
      {
        GSList* subdirs;
        
        subdirs = gconf_engine_all_dirs(client->engine, dirname, NULL);
        
        cache_pairs_in_dir(client, dirname);
      }
      break;

    case GCONF_CLIENT_PRELOAD_RECURSIVE:
      {
        GSList* subdirs;
        
        subdirs = gconf_engine_all_dirs(client->engine, dirname, NULL);
        
        cache_pairs_in_dir(client, dirname);
          
        recurse_subdir_list(client, subdirs, dirname);
      }
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
                              GError** err)
{
  GError* error = NULL;
  
  gconf_engine_set (client->engine, key, val, &error);

  handle_error(client, error, err);
}

gboolean
gconf_client_unset          (GConfClient* client,
                             const gchar* key, GError** err)
{
  GError* error = NULL;
  
  gconf_engine_unset(client->engine, key, &error);

  handle_error(client, error, err);

  if (error != NULL)
    return FALSE;
  else
    return TRUE;
}

GSList*
gconf_client_all_entries    (GConfClient* client,
                             const gchar* dir, GError** err)
{
  GError* error = NULL;
  GSList* retval;
  
  retval = gconf_engine_all_entries(client->engine, dir, &error);

  handle_error(client, error, err);

  return retval;
}

GSList*
gconf_client_all_dirs       (GConfClient* client,
                             const gchar* dir, GError** err)
{
  GError* error = NULL;
  GSList* retval;
  
  retval = gconf_engine_all_dirs(client->engine, dir, &error);

  handle_error(client, error, err);

  return retval;
}

void
gconf_client_suggest_sync   (GConfClient* client,
                             GError** err)
{
  GError* error = NULL;
  
  gconf_engine_suggest_sync(client->engine, &error);

  handle_error(client, error, err);
}

gboolean
gconf_client_dir_exists     (GConfClient* client,
                             const gchar* dir, GError** err)
{
  GError* error = NULL;
  gboolean retval;
  
  retval = gconf_engine_dir_exists(client->engine, dir, &error);

  handle_error(client, error, err);

  return retval;
}

gboolean
gconf_client_key_is_writable(GConfClient* client,
                             const gchar* key,
                             GError**     err)
{
  GError* error = NULL;
  GConfValue* val = NULL;
  gboolean is_writable = TRUE;
  
  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);
  
  val = get (client, key, TRUE,
             NULL, &is_writable, &error);

  if (val == NULL && error != NULL)
    handle_error(client, error, err);
  else
    g_assert(error == NULL);

  /* FIXME we could avoid creating this value at all if
   * we were somewhat less lame.
   */
  if (val)
    gconf_value_free (val);
  
  return is_writable;
}

static gboolean
check_type(const gchar* key, GConfValue* val, GConfValueType t, GError** err)
{
  if (val->type != t)
    {
      gconf_set_error(err, GCONF_ERROR_TYPE_MISMATCH,
                      _("Expected `%s' got `%s' for key %s"),
                      gconf_value_type_to_string(t),
                      gconf_value_type_to_string(val->type),
                      key);
      return FALSE;
    }
  else
    return TRUE;
}

static GConfValue*
get (GConfClient *client,
     const gchar *key,
     gboolean     use_default,
     gboolean    *is_default_retloc,
     gboolean    *is_writable_retloc,
     GError     **error)
{
  GConfValue* val = NULL;
  gboolean is_default = FALSE;
  gboolean is_writable = TRUE;
  
  g_return_val_if_fail(client != NULL, NULL);
  g_return_val_if_fail(GCONF_IS_CLIENT(client), NULL);
  g_return_val_if_fail(error != NULL, NULL);
  g_return_val_if_fail(*error == NULL, NULL);
  
  /* Check our client-side cache */
  if (gconf_client_lookup(client, key, use_default,
                          &is_default,
                          &is_writable,
                          &val))
    {
      if (is_default_retloc)
        *is_default_retloc = is_default;

      if (is_writable_retloc)
        *is_writable_retloc = is_writable;
      
      /* may be NULL of course */
      return val ? gconf_value_copy (val) : NULL;
    }
      
  g_assert(val == NULL); /* if it was in the cache we should have returned */

  /* Check the GConfEngine */
  val = gconf_engine_get_full(client->engine, key,
                              gconf_current_locale(),
                              use_default, &is_default, &is_writable,
                              error);

  if (is_default_retloc)
    *is_default_retloc = is_default;

  if (is_writable_retloc)
    *is_writable_retloc = is_writable;
  
  if (*error != NULL)
    {
      g_return_val_if_fail(val == NULL, NULL);
      return NULL;
    }
  else
    {
      /* Cache this value, if it's in our directory list. FIXME could
       * speed this up.
       */
      gchar* parent = g_strdup(key);
      gchar* end;

      end = strrchr(parent, '/');

      while (end && parent != end)
        {
          *end = '\0';
          
          if (g_hash_table_lookup(client->dir_hash, parent) != NULL)
            {
              /* cache a copy of val */
              gconf_client_cache (client, key, is_default, is_writable,
                                  val ? gconf_value_copy (val) : NULL);
              break;
            }
          
          end = strrchr(parent, '/');
        }

      g_free(parent);

      /* We don't own val, we're returning this copy belonging
       * to the caller
       */
      return val;
    }
}
     
static GConfValue*
gconf_client_get_full        (GConfClient* client,
                              const gchar* key, const gchar* locale,
                              gboolean use_schema_default,
                              gboolean* value_is_default,
                              gboolean* value_is_writable,
                              GError** err)
{
  GError* error = NULL;
  GConfValue* val = NULL;
  gboolean is_default = FALSE;
  gboolean is_writable = TRUE;
  
  g_return_val_if_fail(err == NULL || *err == NULL, NULL);

  if (locale != NULL)
    g_warning("haven't implemented getting a specific locale in GConfClient");
  
  val = get(client, key, use_schema_default,
            &is_default, &is_writable, &error);

  if (val == NULL && error != NULL)
    handle_error(client, error, err);
  else
    g_assert(error == NULL);


  if (value_is_default)
    *value_is_default = is_default;

  if (value_is_writable)
    *value_is_writable = is_writable;

  return val;
}

GConfEntry*
gconf_client_get_entry (GConfClient* client,
                        const gchar* key,
                        const gchar* locale,
                        gboolean use_schema_default,
                        GError** err)
{
  GError* error = NULL;
  GConfValue* val = NULL;
  gboolean is_default = FALSE;
  gboolean is_writable = TRUE;
  GConfEntry *entry;
  
  g_return_val_if_fail(err == NULL || *err == NULL, NULL);

  if (locale != NULL)
    g_warning("haven't implemented getting a specific locale in GConfClient");
  
  val = get(client, key, use_schema_default,
            &is_default, &is_writable, &error);

  if (val == NULL && error != NULL)
    handle_error(client, error, err);
  else
    g_assert(error == NULL);

  entry = gconf_entry_new_nocopy (g_strdup (key), val);
  entry->is_default = is_default;
  entry->is_writable = is_writable;
  
  return entry;
}

GConfValue*
gconf_client_get             (GConfClient* client,
                              const gchar* key,
                              GError** err)
{
  return gconf_client_get_full(client, key, NULL, TRUE, NULL, NULL, err);
}

GConfValue*
gconf_client_get_without_default  (GConfClient* client,
                                   const gchar* key,
                                   GError** err)
{
  return gconf_client_get_full(client, key, NULL, FALSE, NULL, NULL, err);
}

GConfValue*
gconf_client_get_default_from_schema (GConfClient* client,
                                      const gchar* key,
                                      GError** err)
{
  GError* error = NULL;
  GConfValue* val = NULL;
  gboolean is_default = FALSE;
  
  g_return_val_if_fail(err == NULL || *err == NULL, NULL);  
  g_return_val_if_fail(client != NULL, NULL);
  g_return_val_if_fail(GCONF_IS_CLIENT(client), NULL);
  
  /* Check our client-side cache to see if the default is the same as
   the regular value (FIXME put a default_value field in the
   CacheEntry and store both, lose the is_default flag in CacheEntry) */
  if (gconf_client_lookup(client, key, TRUE,
                          &is_default,
                          NULL,
                          &val))
    {        
      if (is_default)
        return val ? gconf_value_copy(val) : NULL;
    }

  /* Check the GConfEngine */
  val = gconf_engine_get_default_from_schema(client->engine, key,
                                             &error);
  
  if (error != NULL)
    {
      g_assert(val == NULL);
      handle_error(client, error, err);
      return NULL;
    }
  else
    {
      /* FIXME eventually we'll cache the value
         by adding a field to CacheEntry */
      return val;
    }
}

gdouble
gconf_client_get_float (GConfClient* client, const gchar* key,
                        GError** err)
{
  static const gdouble def = 0.0;
  GError* error = NULL;
  GConfValue* val;

  g_return_val_if_fail(err == NULL || *err == NULL, 0.0);
  
  val = get(client, key, TRUE, NULL, NULL, &error);

  if (val != NULL)
    {
      gdouble retval = def;

      g_assert(error == NULL);
      
      if (check_type(key, val, GCONF_VALUE_FLOAT, &error))
        retval = gconf_value_get_float(val);
      else
        handle_error(client, error, err);

      gconf_value_free(val);

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
                        GError** err)
{
  static const gint def = 0;
  GError* error = NULL;
  GConfValue* val;

  g_return_val_if_fail(err == NULL || *err == NULL, 0);

  val = get(client, key, TRUE, NULL, NULL, &error);

  if (val != NULL)
    {
      gint retval = def;

      g_assert(error == NULL);
      
      if (check_type(key, val, GCONF_VALUE_INT, &error))
        retval = gconf_value_get_int(val);
      else
        handle_error(client, error, err);

      gconf_value_free(val);

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
                        GError** err)
{
  static const gchar* def = NULL;
  GError* error = NULL;
  GConfValue* val;

  g_return_val_if_fail(err == NULL || *err == NULL, NULL);
  
  val = get(client, key, TRUE, NULL, NULL, &error);

  if (val != NULL)
    {
      gchar* retval = NULL;

      g_assert(error == NULL);
      
      if (check_type(key, val, GCONF_VALUE_STRING, &error))
	/* we cheat here (look below) so we have to cast this */
        retval = (gchar *)gconf_value_get_string(val);
      else
        handle_error(client, error, err);

      /* This is a cheat; don't copy */
      if (retval != NULL)
        val->d.string_data = NULL; /* don't delete the string we are returning */
      else
        retval = def ? g_strdup(def) : NULL;
      
      gconf_value_free(val);

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
                        GError** err)
{
  static const gboolean def = FALSE;
  GError* error = NULL;
  GConfValue* val;

  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);

  val = get(client, key, TRUE, NULL, NULL, &error);  

  if (val != NULL)
    {
      gboolean retval = def;

      g_assert(error == NULL);
      
      if (check_type(key, val, GCONF_VALUE_BOOL, &error))
        retval = gconf_value_get_bool(val);
      else
        handle_error(client, error, err);

      gconf_value_free(val);

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
                          const gchar* key, GError** err)
{
  GError* error = NULL;
  GConfValue* val;

  g_return_val_if_fail(err == NULL || *err == NULL, NULL);

  val = get(client, key, TRUE, NULL, NULL, &error);

  if (val != NULL)
    {
      GConfSchema* retval = NULL;

      g_assert(error == NULL);
      
      if (check_type(key, val, GCONF_VALUE_SCHEMA, &error))
        retval = gconf_value_get_schema(val);
      else
        handle_error(client, error, err);

      /* This is a cheat; don't copy */
      if (retval != NULL)
        val->d.schema_data = NULL; /* don't delete the schema */
      
      gconf_value_free(val);

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
                          GConfValueType list_type, GError** err)
{
  GError* error = NULL;
  GConfValue* val;

  g_return_val_if_fail(err == NULL || *err == NULL, NULL);

  val = get(client, key, TRUE, NULL, NULL, &error);

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
                          GError** err)
{
  GError* error = NULL;
  GConfValue* val;

  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);

  val = get(client, key, TRUE, NULL, NULL, &error);  

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
                          gdouble val, GError** err)
{
  GError* error = NULL;
  
  g_return_val_if_fail(client != NULL, FALSE);
  g_return_val_if_fail(GCONF_IS_CLIENT(client), FALSE);  
  g_return_val_if_fail(key != NULL, FALSE);
  
  if (gconf_engine_set_float(client->engine, key, val, &error))
    return TRUE;
  else
    {
      handle_error(client, error, err);
      return FALSE;
    }
}

gboolean
gconf_client_set_int     (GConfClient* client, const gchar* key,
                          gint val, GError** err)
{
  GError* error = NULL;
  
  g_return_val_if_fail(client != NULL, FALSE);
  g_return_val_if_fail(GCONF_IS_CLIENT(client), FALSE);  
  g_return_val_if_fail(key != NULL, FALSE);
  
  if (gconf_engine_set_int(client->engine, key, val, &error))
    return TRUE;
  else
    {
      handle_error(client, error, err);
      return FALSE;
    }
}

gboolean
gconf_client_set_string  (GConfClient* client, const gchar* key,
                          const gchar* val, GError** err)
{
  GError* error = NULL;
  
  g_return_val_if_fail(client != NULL, FALSE);
  g_return_val_if_fail(GCONF_IS_CLIENT(client), FALSE);  
  g_return_val_if_fail(key != NULL, FALSE);
  g_return_val_if_fail(val != NULL, FALSE);
  
  if (gconf_engine_set_string(client->engine, key, val, &error))
    return TRUE;
  else
    {
      handle_error(client, error, err);
      return FALSE;
    }
}

gboolean
gconf_client_set_bool    (GConfClient* client, const gchar* key,
                          gboolean val, GError** err)
{
  GError* error = NULL;
  
  g_return_val_if_fail(client != NULL, FALSE);
  g_return_val_if_fail(GCONF_IS_CLIENT(client), FALSE);  
  g_return_val_if_fail(key != NULL, FALSE);
  
  if (gconf_engine_set_bool(client->engine, key, val, &error))
    return TRUE;
  else
    {
      handle_error(client, error, err);
      return FALSE;
    }
}

gboolean
gconf_client_set_schema  (GConfClient* client, const gchar* key,
                          GConfSchema* val, GError** err)
{
  GError* error = NULL;
  
  g_return_val_if_fail(client != NULL, FALSE);
  g_return_val_if_fail(GCONF_IS_CLIENT(client), FALSE);  
  g_return_val_if_fail(key != NULL, FALSE);
  g_return_val_if_fail(val != NULL, FALSE);
  
  if (gconf_engine_set_schema(client->engine, key, val, &error))
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
                          GError** err)
{
  GError* error = NULL;
  
  g_return_val_if_fail(client != NULL, FALSE);
  g_return_val_if_fail(GCONF_IS_CLIENT(client), FALSE);  
  g_return_val_if_fail(key != NULL, FALSE);
  
  if (gconf_engine_set_list(client->engine, key, list_type, list, &error))
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
                          GError** err)
{
  GError* error = NULL;
  
  g_return_val_if_fail(client != NULL, FALSE);
  g_return_val_if_fail(GCONF_IS_CLIENT(client), FALSE);  
  g_return_val_if_fail(key != NULL, FALSE);
  
  if (gconf_engine_set_pair(client->engine, key, car_type, cdr_type,
                     address_of_car, address_of_cdr, &error))
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
gconf_client_error                  (GConfClient* client, GError* error)
{
  g_return_if_fail(client != NULL);
  g_return_if_fail(GCONF_IS_CLIENT(client));
  
  gtk_signal_emit(GTK_OBJECT(client), client_signals[ERROR], error);
}

void
gconf_client_unreturned_error       (GConfClient* client, GError* error)
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
                    gboolean is_default,
                    gboolean is_writable,
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
        gconf_value_free(ce->value);

      ce->value = value;
      ce->is_default = is_default;
      ce->is_writable = is_writable;
    }
  else
    {
      /* Create a new entry */
      CacheEntry* ce = cache_entry_new(value, is_default, is_writable);
      g_hash_table_insert(client->cache_hash, g_strdup(key), ce);
    }
}

static gboolean
gconf_client_lookup         (GConfClient* client,
                             const gchar* key,
                             gboolean use_default,
                             gboolean* is_default,
                             gboolean* is_writable,
                             GConfValue** valp)
{
  CacheEntry* ce;

  g_return_val_if_fail(valp != NULL, FALSE);
  g_return_val_if_fail(*valp == NULL, FALSE);
  
  ce = g_hash_table_lookup(client->cache_hash, key);

  if (ce != NULL)
    {
      if (ce->is_default)
        {
          *is_default = TRUE;
          
          if (use_default)
            *valp = ce->value;            
          else
            *valp = NULL;
        }
      else
        {
          *is_default = FALSE;

          *valp = ce->value;
        }

      if (is_writable)
        *is_writable = ce->is_writable;
      
      return TRUE;
    }
  else
    return FALSE;
}


/*
 * CacheEntry
 */ 

static CacheEntry*
cache_entry_new(GConfValue* val,
                gboolean is_default, gboolean is_writable)
{
  CacheEntry* ce;

  ce = g_new(CacheEntry, 1);

  /* val may be NULL */
  ce->value = val;
  ce->is_default = is_default;
  ce->is_writable = is_writable;
  
  return ce;
}

static void
cache_entry_destroy(CacheEntry* ce)
{
  g_return_if_fail(ce != NULL);
  
  if (ce->value != NULL)
    gconf_value_free(ce->value);

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
  d->add_count = 0;
  
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

/*
 * Change sets
 */


struct CommitData {
  GConfClient* client;
  GError* error;
  GSList* remove_list;
  gboolean remove_committed;
};

static void
commit_foreach (GConfChangeSet* cs,
                const gchar* key,
                GConfValue* value,
                gpointer user_data)
{
  struct CommitData* cd = user_data;

  g_assert(cd != NULL);

  if (cd->error != NULL)
    return;
  
  if (value)
    gconf_client_set   (cd->client, key, value, &cd->error);
  else
    gconf_client_unset (cd->client, key, &cd->error);

  if (cd->error == NULL && cd->remove_committed)
    {
      /* Bad bad bad; we keep the key reference, knowing that it's
         valid until we modify the change set, to avoid string copies.  */
      cd->remove_list = g_slist_prepend(cd->remove_list, (gchar*)key);
    }
}

gboolean
gconf_client_commit_change_set   (GConfClient* client,
                                  GConfChangeSet* cs,
                                  gboolean remove_committed,
                                  GError** err)
{
  struct CommitData cd;
  GSList* tmp;

  g_return_val_if_fail(client != NULL, FALSE);
  g_return_val_if_fail(GCONF_IS_CLIENT(client), FALSE);
  g_return_val_if_fail(cs != NULL, FALSE);
  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);
  
  cd.client = client;
  cd.error = NULL;
  cd.remove_list = NULL;
  cd.remove_committed = remove_committed;

  /* Because the commit could have lots of side
     effects, this makes it safer */
  gconf_change_set_ref(cs);
  gtk_object_ref(GTK_OBJECT(client));
  
  gconf_change_set_foreach(cs, commit_foreach, &cd);

  tmp = cd.remove_list;
  while (tmp != NULL)
    {
      const gchar* key = tmp->data;
      
      gconf_change_set_remove(cs, key);

      /* key is now invalid due to our little evil trick */

      tmp = g_slist_next(tmp);
    }

  g_slist_free(cd.remove_list);
  
  gconf_change_set_unref(cs);
  gtk_object_unref(GTK_OBJECT(client));

  if (cd.error != NULL)
    {
      if (err != NULL)
        *err = cd.error;
      else
        g_error_free(cd.error);

      return FALSE;
    }
  else
    {
      g_assert((!remove_committed) ||
               (gconf_change_set_size(cs) == 0));
      
      return TRUE;
    }
}

struct RevertData {
  GConfClient* client;
  GError* error;
  GConfChangeSet* revert_set;
};

static void
revert_foreach (GConfChangeSet* cs,
                const gchar* key,
                GConfValue* value,
                gpointer user_data)
{
  struct RevertData* rd = user_data;
  GConfValue* old_value;
  GError* error = NULL;
  
  g_assert(rd != NULL);

  if (rd->error != NULL)
    return;

  old_value = gconf_client_get_without_default(rd->client, key, &error);

  if (error != NULL)
    {
      /* FIXME */
      g_warning("error creating revert set: %s", error->message);
      g_error_free(error);
      error = NULL;
    }
  
  if (old_value == NULL &&
      value == NULL)
    return; /* this commit will have no effect. */

  if (old_value == NULL)
    gconf_change_set_unset(rd->revert_set, key);
  else
    gconf_change_set_set_nocopy(rd->revert_set, key, old_value);
}


GConfChangeSet*
gconf_client_reverse_change_set  (GConfClient* client,
                                         GConfChangeSet* cs,
                                         GError** err)
{
  struct RevertData rd;

  rd.error = NULL;
  rd.client = client;
  rd.revert_set = gconf_change_set_new();

  /* we're emitting signals and such, avoid
     nasty side effects with these.
  */
  gtk_object_ref(GTK_OBJECT(rd.client));
  gconf_change_set_ref(cs);
  
  gconf_change_set_foreach(cs, revert_foreach, &rd);

  if (rd.error != NULL)
    {
      if (err != NULL)
        *err = rd.error;
      else
        g_error_free(rd.error);
    }

  gtk_object_unref(GTK_OBJECT(rd.client));
  gconf_change_set_unref(cs);
  
  return rd.revert_set;
}


GConfChangeSet*
gconf_client_change_set_from_currentv (GConfClient* client,
                                              const gchar** keys,
                                              GError** err)
{
  GConfValue* old_value;
  GConfChangeSet* new_set;
  const gchar** keyp;
  
  g_return_val_if_fail(err == NULL || *err == NULL, NULL);

  new_set = gconf_change_set_new();
  
  keyp = keys;

  while (*keyp != NULL)
    {
      GError* error = NULL;
      const gchar* key = *keyp;
      
      old_value = gconf_client_get_without_default(client, key, &error);

      if (error != NULL)
        {
          /* FIXME */
          g_warning("error creating change set from current keys: %s", error->message);
          g_error_free(error);
          error = NULL;
        }
      
      if (old_value == NULL)
        gconf_change_set_unset(new_set, key);
      else
        gconf_change_set_set_nocopy(new_set, key, old_value);

      ++keyp;
    }

  return new_set;
}

GConfChangeSet*
gconf_client_change_set_from_current (GConfClient* client,
                                             GError** err,
                                             const gchar* first_key,
                                             ...)
{
  GSList* keys = NULL;
  va_list args;
  const gchar* arg;
  const gchar** vec;
  GConfChangeSet* retval;
  GSList* tmp;
  guint i;
  
  g_return_val_if_fail(err == NULL || *err == NULL, NULL);

  va_start (args, first_key);

  arg = first_key;

  while (arg != NULL)
    {
      keys = g_slist_prepend(keys, (/*non-const*/gchar*)arg);

      arg = va_arg (args, const gchar*);
    }
  
  va_end (args);

  vec = g_new0(const gchar*, g_slist_length(keys) + 1);

  i = 0;
  tmp = keys;

  while (tmp != NULL)
    {
      vec[i] = tmp->data;
      
      ++i;
      tmp = g_slist_next(tmp);
    }

  g_slist_free(keys);
  
  retval = gconf_client_change_set_from_currentv(client, vec, err);
  
  g_free(vec);

  return retval;
}

static GHashTable * clients = NULL;

static void
register_client (GConfClient *client)
{
  if (clients == NULL)
    clients = g_hash_table_new (NULL, NULL);

  g_hash_table_insert (clients, client->engine, client);
}

static GConfClient *
lookup_client (GConfEngine *engine)
{
  if (clients == NULL)
    return NULL;
  else
    return g_hash_table_lookup (clients, engine);
}

static void
unregister_client (GConfClient *client)
{
  g_return_if_fail (clients != NULL);

  g_hash_table_remove (clients, client->engine);
}
