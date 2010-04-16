/*
 * Copyright (C) 2010 Novell, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Vincent Untz <vuntz@gnome.org>
 */

#include "config.h"

#include <string.h>

#include <glib.h>
#include <gio/gio.h>
#include <gconf/gconf-client.h>

#include "gconfsettingsbackend.h"

G_DEFINE_DYNAMIC_TYPE (GConfSettingsBackend, gconf_settings_backend, G_TYPE_SETTINGS_BACKEND);

struct _GConfSettingsBackendPrivate
{
  GConfClient *client;
};

static gboolean
gconf_settings_backend_simple_gconf_value_type_is_compatible (GConfValueType      type,
                                                              const GVariantType *expected_type)
{
  switch (type)
    {
    case GCONF_VALUE_STRING:
      return (g_variant_type_equal (expected_type, G_VARIANT_TYPE_STRING)      ||
              g_variant_type_equal (expected_type, G_VARIANT_TYPE_OBJECT_PATH) ||
              g_variant_type_equal (expected_type, G_VARIANT_TYPE_SIGNATURE));
    case GCONF_VALUE_INT:
      return (g_variant_type_equal (expected_type, G_VARIANT_TYPE_BYTE)   ||
              g_variant_type_equal (expected_type, G_VARIANT_TYPE_INT16)  ||
              g_variant_type_equal (expected_type, G_VARIANT_TYPE_UINT16) ||
              g_variant_type_equal (expected_type, G_VARIANT_TYPE_INT32)  ||
              g_variant_type_equal (expected_type, G_VARIANT_TYPE_UINT32) ||
              g_variant_type_equal (expected_type, G_VARIANT_TYPE_INT64)  ||
              g_variant_type_equal (expected_type, G_VARIANT_TYPE_UINT64) ||
              g_variant_type_equal (expected_type, G_VARIANT_TYPE_HANDLE));
    case GCONF_VALUE_FLOAT:
      return g_variant_type_equal (expected_type, G_VARIANT_TYPE_DOUBLE);
    case GCONF_VALUE_BOOL:
      return g_variant_type_equal (expected_type, G_VARIANT_TYPE_BOOLEAN);
    case GCONF_VALUE_LIST:
    case GCONF_VALUE_PAIR:
      return FALSE;
    default:
      return FALSE;
    }
}

static GVariant *
gconf_settings_backend_simple_gconf_value_type_to_gvariant (GConfValue         *gconf_value,
                                                            const GVariantType *expected_type)
{
  /* Note: it's guaranteed that the types are compatible */
  GVariant *variant = NULL;

  if (g_variant_type_equal (expected_type, G_VARIANT_TYPE_BOOLEAN))
    variant = g_variant_new_boolean (gconf_value_get_bool (gconf_value));
  else if (g_variant_type_equal (expected_type, G_VARIANT_TYPE_BYTE))
    {
      int value = gconf_value_get_int (gconf_value);
      if (value < 0 || value > 255)
        return NULL;
      variant = g_variant_new_byte (value);
    }
  else if (g_variant_type_equal (expected_type, G_VARIANT_TYPE_INT16))
    {
      int value = gconf_value_get_int (gconf_value);
      if (value < G_MINSHORT || value > G_MAXSHORT)
        return NULL;
      variant = g_variant_new_int16 (value);
    }
  else if (g_variant_type_equal (expected_type, G_VARIANT_TYPE_UINT16))
    {
      int value = gconf_value_get_int (gconf_value);
      if (value < 0 || value > G_MAXUSHORT)
        return NULL;
      variant = g_variant_new_uint16 (value);
    }
  else if (g_variant_type_equal (expected_type, G_VARIANT_TYPE_INT32))
    variant = g_variant_new_int32 (gconf_value_get_int (gconf_value));
  else if (g_variant_type_equal (expected_type, G_VARIANT_TYPE_UINT32))
    {
      int value = gconf_value_get_int (gconf_value);
      if (value < 0)
        return NULL;
      variant = g_variant_new_uint32 (value);
    }
  else if (g_variant_type_equal (expected_type, G_VARIANT_TYPE_INT64))
    variant = g_variant_new_int64 ((gint64) gconf_value_get_int (gconf_value));
  else if (g_variant_type_equal (expected_type, G_VARIANT_TYPE_UINT64))
    {
      int value = gconf_value_get_int (gconf_value);
      if (value < 0)
        return NULL;
      variant = g_variant_new_uint64 (value);
    }
  else if (g_variant_type_equal (expected_type, G_VARIANT_TYPE_HANDLE))
    variant = g_variant_new_handle (gconf_value_get_int (gconf_value));
  else if (g_variant_type_equal (expected_type, G_VARIANT_TYPE_DOUBLE))
    variant = g_variant_new_double (gconf_value_get_float (gconf_value));
  else if (g_variant_type_equal (expected_type, G_VARIANT_TYPE_STRING))
    variant = g_variant_new_string (gconf_value_get_string (gconf_value));
  else if (g_variant_type_equal (expected_type, G_VARIANT_TYPE_OBJECT_PATH))
    variant = g_variant_new_object_path (gconf_value_get_string (gconf_value));
  else if (g_variant_type_equal (expected_type, G_VARIANT_TYPE_SIGNATURE))
    variant = g_variant_new_signature (gconf_value_get_string (gconf_value));

  return variant;
}

static GVariant *
gconf_settings_backend_gconf_value_to_gvariant (GConfValue         *gconf_value,
                                                const GVariantType *expected_type)
{
  switch (gconf_value->type)
    {
    case GCONF_VALUE_STRING:
    case GCONF_VALUE_INT:
    case GCONF_VALUE_FLOAT:
    case GCONF_VALUE_BOOL:
      if (!gconf_settings_backend_simple_gconf_value_type_is_compatible (gconf_value->type, expected_type))
        return NULL;
      return gconf_settings_backend_simple_gconf_value_type_to_gvariant (gconf_value, expected_type);
    case GCONF_VALUE_LIST:
      {
        GConfValueType      list_type;
        const GVariantType *array_type;
        GSList             *list;
        GPtrArray          *array;
        GVariant           *result;

        if (!g_variant_type_is_array (expected_type))
          return NULL;

        list_type = gconf_value_get_list_type (gconf_value);
        array_type = g_variant_type_element (expected_type);
        if (!gconf_settings_backend_simple_gconf_value_type_is_compatible (list_type, array_type))
          return NULL;

        array = g_ptr_array_new ();
        for (list = gconf_value_get_list (gconf_value); list != NULL; list = list->next)
          {
            GVariant *variant;
            variant = gconf_settings_backend_simple_gconf_value_type_to_gvariant (list->data, array_type);
            g_ptr_array_add (array, variant);
          }

        result = g_variant_new_array (array_type, (GVariant **) array->pdata, array->len);
        g_ptr_array_free (array, TRUE);

        return result;
      }
      break;
    case GCONF_VALUE_PAIR:
      {
        GConfValue         *car;
        GConfValue         *cdr;
        const GVariantType *first_type;
        const GVariantType *second_type;
        GVariant           *tuple[2];
        GVariant           *result;

        if (!g_variant_type_is_tuple (expected_type) ||
            g_variant_type_n_items (expected_type) != 2)
          return NULL;

        car = gconf_value_get_car (gconf_value);
        cdr = gconf_value_get_cdr (gconf_value);
        first_type = g_variant_type_first (expected_type);
        second_type = g_variant_type_next (first_type);

        if (!gconf_settings_backend_simple_gconf_value_type_is_compatible (car->type, first_type) ||
            !gconf_settings_backend_simple_gconf_value_type_is_compatible (cdr->type, second_type))
          return NULL;

        tuple[0] = gconf_settings_backend_simple_gconf_value_type_to_gvariant (car, first_type);
        tuple[1] = gconf_settings_backend_simple_gconf_value_type_to_gvariant (cdr, second_type);

        result = g_variant_new_tuple (tuple, 2);
        return result;
      }
      break;
    default:
      return NULL;
    }

  g_assert_not_reached ();

  return NULL;
}

static GConfValueType
gconf_settings_backend_simple_gvariant_type_to_gconf_value_type (const GVariantType *type)
{
  if (g_variant_type_equal (type, G_VARIANT_TYPE_BOOLEAN))
      return GCONF_VALUE_BOOL;
  else if (g_variant_type_equal (type, G_VARIANT_TYPE_BYTE)     ||
           g_variant_type_equal (type, G_VARIANT_TYPE_INT16)    ||
           g_variant_type_equal (type, G_VARIANT_TYPE_UINT16)   ||
           g_variant_type_equal (type, G_VARIANT_TYPE_INT32)    ||
           g_variant_type_equal (type, G_VARIANT_TYPE_UINT32)   ||
           g_variant_type_equal (type, G_VARIANT_TYPE_INT64)    ||
           g_variant_type_equal (type, G_VARIANT_TYPE_UINT64)   ||
           g_variant_type_equal (type, G_VARIANT_TYPE_HANDLE))
      return GCONF_VALUE_INT;
  else if (g_variant_type_equal (type, G_VARIANT_TYPE_DOUBLE))
      return GCONF_VALUE_FLOAT;
  else if (g_variant_type_equal (type, G_VARIANT_TYPE_STRING)      ||
           g_variant_type_equal (type, G_VARIANT_TYPE_OBJECT_PATH) ||
           g_variant_type_equal (type, G_VARIANT_TYPE_SIGNATURE))
      return GCONF_VALUE_STRING;

  return GCONF_VALUE_INVALID;
}

static GConfValue *
gconf_settings_backend_simple_gvariant_to_gconf_value (GVariant           *value,
                                                       const GVariantType *type)
{
  GConfValue *gconf_value = NULL;

  if (g_variant_type_equal (type, G_VARIANT_TYPE_BOOLEAN))
    {
      gconf_value = gconf_value_new (GCONF_VALUE_BOOL);
      gconf_value_set_bool (gconf_value, g_variant_get_boolean (value));
    }
  else if (g_variant_type_equal (type, G_VARIANT_TYPE_BYTE))
    {
      guchar i = g_variant_get_byte (value);
      gconf_value = gconf_value_new (GCONF_VALUE_INT);
      gconf_value_set_int (gconf_value, i);
    }
  else if (g_variant_type_equal (type, G_VARIANT_TYPE_INT16))
    {
      gint16 i = g_variant_get_int16 (value);
      gconf_value = gconf_value_new (GCONF_VALUE_INT);
      gconf_value_set_int (gconf_value, i);
    }
  else if (g_variant_type_equal (type, G_VARIANT_TYPE_UINT16))
    {
      guint16 i = g_variant_get_uint16 (value);
      if (i > G_MAXINT)
        return NULL;
      gconf_value = gconf_value_new (GCONF_VALUE_INT);
      gconf_value_set_int (gconf_value, i);
    }
  else if (g_variant_type_equal (type, G_VARIANT_TYPE_INT32))
    {
      gint32 i = g_variant_get_int32 (value);
      gconf_value = gconf_value_new (GCONF_VALUE_INT);
      gconf_value_set_int (gconf_value, i);
    }
  else if (g_variant_type_equal (type, G_VARIANT_TYPE_UINT32))
    {
      guint32 i = g_variant_get_uint32 (value);
      if (i > G_MAXINT)
        return NULL;
      gconf_value = gconf_value_new (GCONF_VALUE_INT);
      gconf_value_set_int (gconf_value, i);
    }
  else if (g_variant_type_equal (type, G_VARIANT_TYPE_INT64))
    {
      gint64 i = g_variant_get_int64 (value);
      if (i < G_MININT || i > G_MAXINT)
        return NULL;
      gconf_value = gconf_value_new (GCONF_VALUE_INT);
      gconf_value_set_int (gconf_value, i);
    }
  else if (g_variant_type_equal (type, G_VARIANT_TYPE_UINT64))
    {
      guint64 i = g_variant_get_uint64 (value);
      if (i > G_MAXINT)
        return NULL;
      gconf_value = gconf_value_new (GCONF_VALUE_INT);
      gconf_value_set_int (gconf_value, i);
    }
  else if (g_variant_type_equal (type, G_VARIANT_TYPE_HANDLE))
    {
      gint32 i = g_variant_get_handle (value);
      gconf_value = gconf_value_new (GCONF_VALUE_INT);
      gconf_value_set_int (gconf_value, i);
    }
  else if (g_variant_type_equal (type, G_VARIANT_TYPE_DOUBLE))
    {
      gconf_value = gconf_value_new (GCONF_VALUE_FLOAT);
      gconf_value_set_float (gconf_value, g_variant_get_double (value));
    }
  else if (g_variant_type_equal (type, G_VARIANT_TYPE_STRING)      ||
           g_variant_type_equal (type, G_VARIANT_TYPE_OBJECT_PATH) ||
           g_variant_type_equal (type, G_VARIANT_TYPE_SIGNATURE))
    {
      gconf_value = gconf_value_new (GCONF_VALUE_STRING);
      gconf_value_set_string (gconf_value, g_variant_get_string (value, NULL));
    }

  return gconf_value;
}

static GConfValue *
gconf_settings_backend_gvariant_to_gconf_value (GVariant *value)
{
  const GVariantType *type;
  GConfValue         *gconf_value = NULL;

  type = g_variant_get_type (value);
  if (g_variant_type_is_basic (type) &&
      !g_variant_type_equal (type, G_VARIANT_TYPE_BASIC))
    gconf_value = gconf_settings_backend_simple_gvariant_to_gconf_value (value, type);
  else if (g_variant_type_is_array (type))
    {
      const GVariantType *array_type;
      array_type = g_variant_type_element (type);

      if (g_variant_type_is_basic (array_type) &&
          !g_variant_type_equal (array_type, G_VARIANT_TYPE_BASIC))
        {
          GConfValueType  value_type;
          int             i;
          GSList        *list = NULL;

          for (i = 0; i < g_variant_n_children (value); i++)
            {
              GConfValue *l;

              l = gconf_settings_backend_simple_gvariant_to_gconf_value (g_variant_get_child_value (value, i),
                                                                         array_type);
              list = g_slist_prepend (list, l);
            }

          list = g_slist_reverse (list);

          value_type = gconf_settings_backend_simple_gvariant_type_to_gconf_value_type (array_type);
          gconf_value = gconf_value_new (GCONF_VALUE_LIST);
          gconf_value_set_list_type (gconf_value, value_type);
          gconf_value_set_list (gconf_value, list);

          g_slist_foreach (list, (GFunc) gconf_value_free, NULL);
          g_slist_free (list);
        }
    }
  else if (g_variant_type_is_tuple (type) &&
            g_variant_type_n_items (type) == 2)
    {
      const GVariantType *first_type;
      const GVariantType *second_type;

      first_type = g_variant_type_first (type);
      second_type = g_variant_type_next (first_type);

      if (g_variant_type_is_basic (first_type) &&
          !g_variant_type_equal (first_type, G_VARIANT_TYPE_BASIC) &&
          g_variant_type_is_basic (second_type) &&
          !g_variant_type_equal (second_type, G_VARIANT_TYPE_BASIC))
        {
          GConfValue *car;
          GConfValue *cdr;

          gconf_value = gconf_value_new (GCONF_VALUE_PAIR);

          car = gconf_settings_backend_simple_gvariant_to_gconf_value (g_variant_get_child_value (value, 0), first_type);
          cdr = gconf_settings_backend_simple_gvariant_to_gconf_value (g_variant_get_child_value (value, 1), second_type);

          if (car)
            gconf_value_set_car_nocopy (gconf_value, car);
          if (cdr)
            gconf_value_set_cdr_nocopy (gconf_value, cdr);

          if (car == NULL || cdr == NULL)
            {
              gconf_value_free (gconf_value);
              gconf_value = NULL;
            }
        }
    }

  return gconf_value;
}

static GVariant *
gconf_settings_backend_read (GSettingsBackend   *backend,
                             const gchar        *key,
                             const GVariantType *expected_type)
{
  GConfSettingsBackend *gconf = GCONF_SETTINGS_BACKEND (backend);
  GConfValue *gconf_value;
  GVariant *value;

  gconf_value = gconf_client_get_without_default (gconf->priv->client,
                                                  key, NULL);
  if (gconf_value == NULL)
    return NULL;

  value = gconf_settings_backend_gconf_value_to_gvariant (gconf_value, expected_type);
  gconf_value_free (gconf_value);

  if (value != NULL)
    g_variant_ref_sink (value);

  return value;
}

static gboolean
gconf_settings_backend_write (GSettingsBackend *backend,
                              const gchar      *key,
                              GVariant         *value,
                              gpointer          origin_tag)
{
  GConfSettingsBackend *gconf = GCONF_SETTINGS_BACKEND (backend);
  GConfValue           *gconf_value;
  GError               *error;

  gconf_value = gconf_settings_backend_gvariant_to_gconf_value (value);
  if (gconf_value == NULL)
    return FALSE;

  error = NULL;
  gconf_client_set (gconf->priv->client, key, gconf_value, &error);
  gconf_value_free (gconf_value);

  if (error != NULL)
    {
      g_error_free (error);
      return FALSE;
    }

  g_settings_backend_changed (backend, key, origin_tag);

  return TRUE;
  //FIXME: eat gconf notification for the change we just did
}

static gboolean
gconf_settings_backend_write_one_to_changeset (const gchar    *key,
                                               GVariant       *value,
                                               GConfChangeSet *changeset)
{
  GConfValue *gconf_value;

  gconf_value = gconf_settings_backend_gvariant_to_gconf_value (value);
  if (gconf_value == NULL)
    return TRUE;

  gconf_change_set_set_nocopy (changeset, key, gconf_value);

  return FALSE;
}

static gboolean
gconf_settings_backend_write_keys (GSettingsBackend *backend,
                                   GTree            *tree,
                                   gpointer          origin_tag)
{
  GConfSettingsBackend *gconf = GCONF_SETTINGS_BACKEND (backend);
  GConfChangeSet       *changeset;
  GConfChangeSet       *reversed;
  gboolean              success;

  changeset = gconf_change_set_new ();

  g_tree_foreach (tree, (GTraverseFunc) gconf_settings_backend_write_one_to_changeset, changeset);

  if (gconf_change_set_size (changeset) != g_tree_nnodes (tree))
    {
      gconf_change_set_unref (changeset);
      return FALSE;
    }

  reversed = gconf_client_reverse_change_set (gconf->priv->client, changeset, NULL);
  success = gconf_client_commit_change_set (gconf->priv->client, changeset, FALSE, NULL);

  if (!success)
    gconf_client_commit_change_set (gconf->priv->client, reversed, FALSE, NULL);
  else
    g_settings_backend_changed_tree (backend, tree, origin_tag);

  gconf_change_set_unref (changeset);
  gconf_change_set_unref (reversed);

  return success;
}

static void
gconf_settings_backend_reset (GSettingsBackend *backend,
                              const gchar      *key,
                              gpointer          origin_tag)
{
  GConfSettingsBackend *gconf = GCONF_SETTINGS_BACKEND (backend);

  if (gconf_client_unset (gconf->priv->client, key, NULL))
    g_settings_backend_changed (backend, key, origin_tag);
}

static void
gconf_settings_backend_reset_path (GSettingsBackend *backend,
                                   const gchar      *path,
                                   gpointer          origin_tag)
{
  GConfSettingsBackend *gconf = GCONF_SETTINGS_BACKEND (backend);

  /* We have no way to know if it was completely successful or if it
   * completely failed, or if only some keys were unset, so we just send
   * one big changed signal. */
  gconf_client_recursive_unset (gconf->priv->client, path, 0, NULL);
  g_settings_backend_path_changed (backend, path, origin_tag);
}

static gboolean
gconf_settings_backend_get_writable (GSettingsBackend *backend,
                                     const gchar      *name)
{
  GConfSettingsBackend *gconf = GCONF_SETTINGS_BACKEND (backend);

  /* We don't support checking writabality for a whole subpath, so we just say
   * it's not writable in such a case. */
  if (name[strlen(name) - 1] == '/')
    return FALSE;

  return gconf_client_key_is_writable (gconf->priv->client, name, NULL);
}

static char *
gconf_settings_backend_get_gconf_path_from_name (const gchar *name)
{
  /* We don't want trailing slash since gconf directories shouldn't have a
   * trailing slash. */
  if (name[strlen(name) - 1] != '/')
    {
      const gchar *slash;
      slash = strrchr (name, '/');
      g_assert (slash != NULL);
      return g_strndup (name, slash - name);
    }
  else
    return g_strndup (name, strlen(name) - 1);
}

static void
gconf_settings_backend_subscribe (GSettingsBackend *backend,
                                  const gchar      *name)
{
  GConfSettingsBackend *gconf = GCONF_SETTINGS_BACKEND (backend);
  gchar                *path;

  path = gconf_settings_backend_get_gconf_path_from_name (name);
  gconf_client_add_dir (gconf->priv->client, path, GCONF_CLIENT_PRELOAD_ONELEVEL, NULL);
  g_free (path);
  //FIXME notify
}

static void
gconf_settings_backend_unsubscribe (GSettingsBackend *backend,
                                    const gchar      *name)
{
  GConfSettingsBackend *gconf = GCONF_SETTINGS_BACKEND (backend);
  gchar                *path;

  path = gconf_settings_backend_get_gconf_path_from_name (name);
  gconf_client_remove_dir (gconf->priv->client, path, NULL);
  g_free (path);
}

static gboolean
gconf_settings_backend_supports_context (const gchar *context)
{
  return FALSE;
}

static void
gconf_settings_backend_finalize (GObject *object)
{
  GConfSettingsBackend *gconf = GCONF_SETTINGS_BACKEND (object);

  g_object_unref (gconf->priv->client);
  gconf->priv->client = NULL;

  G_OBJECT_CLASS (gconf_settings_backend_parent_class)
    ->finalize (object);
}

static void
gconf_settings_backend_init (GConfSettingsBackend *gconf)
{
  gconf->priv = G_TYPE_INSTANCE_GET_PRIVATE (gconf,
                                             GCONF_TYPE_SETTINGS_BACKEND,
                                             GConfSettingsBackendPrivate);
  gconf->priv->client = gconf_client_get_default ();
}

static void
gconf_settings_backend_class_finalize (GConfSettingsBackendClass *class)
{                               
}

static void
gconf_settings_backend_class_init (GConfSettingsBackendClass *class)
{
  GSettingsBackendClass *backend_class = G_SETTINGS_BACKEND_CLASS (class);
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  object_class->finalize = gconf_settings_backend_finalize;

  backend_class->read = gconf_settings_backend_read;
  backend_class->write = gconf_settings_backend_write;
  backend_class->write_keys = gconf_settings_backend_write_keys;
  backend_class->reset = gconf_settings_backend_reset;
  backend_class->reset_path = gconf_settings_backend_reset_path;
  backend_class->get_writable = gconf_settings_backend_get_writable;
  backend_class->subscribe = gconf_settings_backend_subscribe;
  backend_class->unsubscribe = gconf_settings_backend_unsubscribe;
  backend_class->supports_context = gconf_settings_backend_supports_context;

  g_type_class_add_private (class, sizeof (GConfSettingsBackendPrivate));
}

void 
gconf_settings_backend_register (GIOModule *module)
{
  gconf_settings_backend_register_type (G_TYPE_MODULE (module));
  g_io_extension_point_implement (G_SETTINGS_BACKEND_EXTENSION_POINT_NAME,
                                  GCONF_TYPE_SETTINGS_BACKEND,
                                  "gconf",
                                  -1);
}
