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

#include "gconf-gtk.h"

enum {
  VALUE_CHANGED,
  UNRETURNED_ERROR,
  ERROR,
  LAST_SIGNAL
};

/* FIXME cache-entry and dir-in-list objects are needed */


static void gconf_client_class_init (GConfClientClass *klass);
static void gconf_client_init       (GConfClient      *client);
static void gconf_client_real_unreturned_error (GConfClient* client, GConfError* error);
static void gconf_client_real_error            (GConfClient* client, GConfError* error);
static void gconf_client_destroy               (GtkObject* object); 

static void gconf_client_cache                 (GConfClient* client,
                                                const gchar* key,
                                                GConfValue* value); /* takes ownership of value */

static GConfValue* gconf_client_lookup         (GConfClient* client,
                                                const gchar* key);

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
}

static void
gconf_client_destroy               (GtkObject* object)
{
  GConfClient* client = GCONF_CLIENT(object);

  if (client->engine != NULL)
    {
      gconf_engine_unref(client->engine);
      client->engine = NULL;
    }

  if (client->cache_hash != NULL)
    {
      /* FIXME destroy each item in the hash */
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

}

static void
gconf_client_real_error            (GConfClient* client, GConfError* error)
{
  /* FIXME use dialogs in an idle function, depending on the error mode */

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
                          const gchar* dir_orig,
                          GConfClientPreloadType preload,
                          GConfError** err)
{
  gchar* dir;
  
  dir = g_strdup(dir_orig);

#ifndef G_DISABLE_CHECKS
  {
    GSList* tmp;
    
    tmp = client->dir_list;

    while (tmp != NULL)
      {
        gchar* str = tmp->data;
        
        /* Disallow overlap */
        g_return_if_fail(!gconf_key_is_below(str, dir));
        g_return_if_fail(!gconf_key_is_below(dir, str));
        
        tmp = g_slist_next(tmp);
      }
  }
#endif

  /* FIXME need a "dir" object so we can store notify ID, etc. */
  
  client->dir_list = g_slist_prepend(client->dir_list, dir);

  switch (preload)
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

  /* FIXME add notify to GConfEngine for this dir */
  
}

void
gconf_client_remove_dir  (GConfClient* client,
                          const gchar* dir)
{
  /* FIXME remove dir from list */
  /* FIXME remove notify for this dir */

}



guint
gconf_client_notify_add(GConfClient* client,
                        const gchar* namespace_section,
                        GConfClientNotifyFunc func,
                        gpointer user_data,
                        GConfError** err)
{
  /* FIXME add to the GConfListeners */

}

void
gconf_client_notify_remove  (GConfClient* client,
                             guint cnxn)
{
  /* FIXME remove from the GConfListeners */
  
}


void
gconf_client_set_error_handling(GConfClient* client,
                                GConfClientErrorHandlingMode mode,
                                /* func can be NULL for none or N/A */
                                GConfClientParentWindowFunc func,
                                gpointer user_data)
{
  /* FIXME just fills in some fields in GConfClient */

}

/*
 * Basic key-manipulation facilities
 */

static void
handle_error(GConfClient* client, GConfError* error, GConfError** err)
{
  if (error != NULL)
    {
      gtk_signal_emit(GTK_OBJECT(client), client_signals[ERROR], error);
      
      if (err == NULL)
        {
          gtk_signal_emit(GTK_OBJECT(client), client_signals[UNRETURNED_ERROR], error);
          gconf_error_destroy(error);
        }
      else
        *err = error;
    }
}

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
  GConfValue* val;

  val = gconf_client_lookup(client, key);

  if (val == NULL)
    {
      val = gconf_get(client->engine, key, &error);
      
      handle_error(client, error, err);
    }
      
  return val;
}

gboolean
gconf_client_unset          (GConfClient* client,
                             const gchar* key, GConfError** err)
{
  GConfError* error = NULL;
  
  gconf_unset(client->engine, key, &error);

  handle_error(client, error, err);
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
  
  retval = gconf_all_dirs(client->engine, dir, &error);

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

gdouble
gconf_client_get_float (GConfClient* client, const gchar* key,
                        gdouble def, GConfError** err)
{
  GConfError* error = NULL;
  GConfValue* val;
  gdouble retval;
  
  val = gconf_client_lookup(client, key);

  if (val == NULL)
    {
      val = gconf_get(client->engine, key, &error);

      if (error != NULL)
        {
          g_return_val_if_fail(val == NULL, def);
          handle_error(client, error, err);
          return def;
        }
    }

  if (val != NULL)
    {
      if (check_type(val, GCONF_VALUE_FLOAT, &error))
        retval = gconf_value_float(val);
      else
        {
          retval = def;
          handle_error(client, error, err);
        }
      gconf_value_destroy(val);
    }
        
  return retval;
}

gint
gconf_client_get_int   (GConfClient* client, const gchar* key,
                        gint def, GConfError** err)
{


}

gchar*
gconf_client_get_string(GConfClient* client, const gchar* key,
                        const gchar* def, 
                        GConfError** err)
{


}


gboolean
gconf_client_get_bool  (GConfClient* client, const gchar* key,
                        gboolean def, GConfError** err)
{


}

GConfSchema*
gconf_client_get_schema  (GConfClient* client,
                          const gchar* key, GConfError** err)
{


}

gboolean
gconf_client_set_float   (GConfClient* client, const gchar* key,
                          gdouble val, GConfError** err)
{


}

gboolean
gconf_client_set_int     (GConfClient* client, const gchar* key,
                          gint val, GConfError** err)
{

}

gboolean
gconf_client_set_string  (GConfClient* client, const gchar* key,
                          const gchar* val, GConfError** err)
{


}

gboolean
gconf_client_set_bool    (GConfClient* client, const gchar* key,
                          gboolean val, GConfError** err)
{

}

gboolean
gconf_client_set_schema  (GConfClient* client, const gchar* key,
                          GConfSchema* val, GConfError** err)
{


}

/*
 * Functions to emit signals
 */

void
gconf_client_error                  (GConfClient* client, GConfError* error)
{


}

void
gconf_client_unreturned_error       (GConfClient* client, GConfError* error)
{


}

void
gconf_client_value_changed          (GConfClient* client,
                                     const gchar* key,
                                     GConfValue* value)
{


}

/*
 * Internal utility
 */

static void
gconf_client_cache (GConfClient* client,
                    const gchar* key,
                    GConfValue* value)
{
  gpointer oldkey = NULL, oldval = NULL;

  if (g_hash_table_lookup_extended(client->cache_hash, key, &oldkey, &oldval))
    {
      g_assert(oldval != NULL);
      gconf_value_destroy(oldval);
    }

  /* re-use the same oldkey, but replace the value */
  g_hash_table_insert(client->cache_hash, oldkey, value);
}

static GConfValue*
gconf_client_lookup         (GConfClient* client,
                             const gchar* key)
{
  return g_hash_table_lookup(client->cache_hash, key);
}
