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

#include "gconf-changeset.h"

typedef enum {
  CHANGE_INVALID,
  CHANGE_SET,
  CHANGE_UNSET
} ChangeType;

typedef struct _Change Change;

struct _Change {
  gchar* key;
  ChangeType type;
  GConfValue* value;
};

Change* change_new    (const gchar* key);
void    change_set    (Change* c, GConfValue* value);
void    change_unset  (Change* c);
void    change_destroy(Change* c);

struct _GConfChangeSet {
  guint refcount;
  GHashTable* hash;
};

GConfChangeSet*
gconf_change_set_new      (void)
{
  GConfChangeSet* cs;

  cs = g_new(GConfChangeSet, 1);

  cs->refcount = 1;
  cs->hash = g_hash_table_new(g_str_hash, g_str_equal);

  return cs;
}

void
gconf_change_set_ref      (GConfChangeSet* cs)
{
  g_return_if_fail(cs != NULL);
  
  cs->refcount += 1;
}

void
gconf_change_set_unref    (GConfChangeSet* cs)
{
  g_return_if_fail(cs != NULL);
  g_return_if_fail(cs->refcount > 0);

  cs->refcount -= 1;

  if (cs->refcount == 0)
    {
      gconf_change_set_clear(cs);

      g_hash_table_destroy(cs->hash);
      
      g_free(cs);
    }
}

static Change*
get_change_unconditional (GConfChangeSet* cs,
                          const gchar* key)
{
  Change* c;

  c = g_hash_table_lookup(cs->hash, key);

  if (c == NULL)
    {
      c = change_new(key);

      g_hash_table_insert(cs->hash, c->key, c);
    }

  return c;
}

static gboolean
destroy_foreach (gpointer key, gpointer value, gpointer user_data)
{
  Change* c = value;

  g_assert(c != NULL);

  change_destroy(c);

  return TRUE; /* remove from hash */
}

void
gconf_change_set_clear    (GConfChangeSet* cs)
{
  g_return_if_fail(cs != NULL);

  g_hash_table_foreach_remove (cs->hash, destroy_foreach, NULL);
}

void
gconf_change_set_remove   (GConfChangeSet* cs,
                           const gchar* key)
{
  Change* c;
  
  g_return_if_fail(cs != NULL);

  c = g_hash_table_lookup(cs->hash, key);

  if (c != NULL)
    {
      g_hash_table_remove(cs->hash, c->key);
      change_destroy(c);
    }
}


struct ForeachData {
  GConfChangeSet* cs;
  GConfChangeSetForeachFunc func;
  gpointer user_data;
};

static void
foreach(gpointer key, gpointer value, gpointer user_data)
{
  Change* c;
  struct ForeachData* fd = user_data;
  
  c = value;

  /* assumes that an UNSET change has a NULL value */
  (* fd->func) (fd->cs, c->key, c->value, fd->user_data);
}

void
gconf_change_set_foreach  (GConfChangeSet* cs,
                           GConfChangeSetForeachFunc func,
                           gpointer user_data)
{
  struct ForeachData fd;
  
  g_return_if_fail(cs != NULL);
  g_return_if_fail(func != NULL);

  fd.cs = cs;
  fd.func = func;
  fd.user_data = user_data;

  gconf_change_set_ref(cs);
  
  g_hash_table_foreach(cs->hash, foreach, &fd);

  gconf_change_set_unref(cs);
}

void
gconf_change_set_set_nocopy  (GConfChangeSet* cs, const gchar* key,
                              GConfValue* value)
{
  Change* c;
  
  g_return_if_fail(cs != NULL);
  g_return_if_fail(value != NULL);

  c = get_change_unconditional(cs, key);

  change_set(c, value);
}

void
gconf_change_set_set (GConfChangeSet* cs, const gchar* key,
                      GConfValue* value)
{
  g_return_if_fail(value != NULL);
  
  gconf_change_set_set(cs, key, gconf_value_copy(value));
}

void
gconf_changet_set_unset      (GConfChangeSet* cs, const gchar* key)
{
  Change* c;
  
  g_return_if_fail(cs != NULL);

  c = get_change_unconditional(cs, key);

  change_unset(c);
}

void
gconf_change_set_set_float   (GConfChangeSet* cs, const gchar* key,
                              gdouble val)
{
  GConfValue* value;
  
  g_return_if_fail(cs != NULL);

  value = gconf_value_new(GCONF_VALUE_FLOAT);
  gconf_value_set_float(value, val);
  
  gconf_change_set_set_nocopy(cs, key, value);
}

void
gconf_change_set_set_int     (GConfChangeSet* cs, const gchar* key,
                              gint val)
{
  GConfValue* value;
  
  g_return_if_fail(cs != NULL);

  value = gconf_value_new(GCONF_VALUE_INT);
  gconf_value_set_int(value, val);
  
  gconf_change_set_set_nocopy(cs, key, value);
}

void
gconf_change_set_set_string  (GConfChangeSet* cs, const gchar* key,
                              const gchar* val)
{
  GConfValue* value;
  
  g_return_if_fail(cs != NULL);

  value = gconf_value_new(GCONF_VALUE_STRING);
  gconf_value_set_string(value, val);
  
  gconf_change_set_set_nocopy(cs, key, value);
}

void
gconf_change_set_set_bool    (GConfChangeSet* cs, const gchar* key,
                              gboolean val)
{
  GConfValue* value;
  
  g_return_if_fail(cs != NULL);

  value = gconf_value_new(GCONF_VALUE_BOOL);
  gconf_value_set_bool(value, val);
  
  gconf_change_set_set_nocopy(cs, key, value);
}

void
gconf_change_set_set_schema  (GConfChangeSet* cs, const gchar* key,
                              GConfSchema* val)
{
  GConfValue* value;
  
  g_return_if_fail(cs != NULL);

  value = gconf_value_new(GCONF_VALUE_SCHEMA);
  gconf_value_set_schema(value, val);
  
  gconf_change_set_set_nocopy(cs, key, value);
}

/*
 * Change
 */

Change*
change_new    (const gchar* key)
{
  Change* c;

  c = g_new(Change, 1);

  c->key  = g_strdup(key);
  c->type = CHANGE_INVALID;
  c->value = NULL;

  return c;
}

void
change_destroy(Change* c)
{
  g_return_if_fail(c != NULL);
  
  g_free(c->key);

  if (c->value)
    gconf_value_destroy(c->value);

  g_free(c);
}

void
change_set    (Change* c, GConfValue* value)
{
  c->type = CHANGE_SET;
  
  if (c->value)
    gconf_value_destroy(c->value);

  c->value = value;
}

void
change_unset  (Change* c)
{
  c->type = CHANGE_UNSET;

  if (c->value)
    gconf_value_destroy(c->value);

  c->value = NULL;
}

/*
 * Actually send it upstream
 */

struct CommitData {
  GConfEngine* conf;
  GConfError* error;
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
    gconf_set   (cd->conf, key, value, &cd->error);
  else
    gconf_unset (cd->conf, key, &cd->error);

  if (cd->error == NULL && cd->remove_committed)
    {
      /* Bad bad bad; we keep the key reference, knowing that it's
         valid until we modify the change set, to avoid string copies.  */
      cd->remove_list = g_slist_prepend(cd->remove_list, (gchar*)key);
    }
}

gboolean
gconf_commit_change_set   (GConfEngine* conf,
                           GConfChangeSet* cs,
                           gboolean remove_committed,
                           GConfError** err)
{
  struct CommitData cd;
  GSList* tmp;
  
  cd.conf = conf;
  cd.error = NULL;
  cd.remove_list = NULL;
  cd.remove_committed = remove_committed;

  /* Because the commit could have lots of side
     effects, this makes it safer */
  gconf_change_set_ref(cs);
  gconf_engine_ref(conf);
  
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
  gconf_engine_unref(conf);

  if (cd.error != NULL)
    {
      if (err != NULL)
        *err = cd.error;
      else
        gconf_error_destroy(cd.error);

      return FALSE;
    }
  else
    {
      return TRUE;
    }
}
