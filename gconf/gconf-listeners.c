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

#include "gconf-listeners.h"

typedef struct _Listener Listener;

struct _Listener {
  guint cnxn;
  gpointer listener_data;
  GFreeFunc destroy_notify;
};

/* LTable is GConfListenersPrivate, but shorter */

typedef struct _LTable LTable;

struct _LTable {
  GNode* tree; /* Represents the config "filesystem" namespace. 
                *  Kept sorted. 
                */
  GPtrArray* listeners; /* Listeners are also kept in a flat array here, indexed by connection number */
  guint active_listeners; /* count of "alive" listeners */
  /* 0 is an error value */
  guint next_cnxn;

  /* Connection numbers to be recycled - somewhat dangerous due to the nature
     of CORBA... */
  GSList* removed_cnxns;
};

typedef struct _LTableEntry LTableEntry;

struct _LTableEntry {
  gchar* name; /* The name of this "directory" */
  GList* listeners; /* Each listener listening *exactly* here. You probably 
                        want to notify all listeners *below* this node as well. 
                     */
};

static LTable* ltable_new(void);
static void    ltable_insert(LTable* ltable,
                             const gchar* where,
                             Listener* listener);
static void    ltable_remove(LTable* ltable,
                             guint cnxn);
static void    ltable_destroy(LTable* ltable);
static void    ltable_notify(LTable* ltable,
                             const gchar* key,
                             GConfListenersCallback callback,
                             gpointer user_data);

static guint   ltable_next_cnxn(LTable* ltable);

#if 0
static void    ltable_spew(LTable* ltable);
#endif

static LTableEntry* ltable_entry_new(const gchar* name);
static void         ltable_entry_destroy(LTableEntry* entry);

static Listener* listener_new(guint cnxn_id, gpointer listener_data, GFreeFunc destroy_notify);
static void listener_destroy(Listener* l);

/*
 * Public API
 */ 

GConfListeners*
gconf_listeners_new     (void)
{
  LTable* lt;

  lt = ltable_new();

  return (GConfListeners*)lt;
}

void
gconf_listeners_destroy (GConfListeners* listeners)
{
  LTable* lt = (LTable*)listeners;

  ltable_destroy(lt);
}

guint
gconf_listeners_add     (GConfListeners* listeners,
                         const gchar* listen_point,
                         gpointer listener_data,
                         GFreeFunc destroy_notify)
{
  LTable* lt = (LTable*)listeners;
  Listener* l;

  l = listener_new(ltable_next_cnxn(lt), listener_data, destroy_notify);
  
  ltable_insert(lt, listen_point, l);

  return l->cnxn;
}

void
gconf_listeners_remove  (GConfListeners* listeners,
                         guint cnxn_id)
{
  LTable* lt = (LTable*)listeners;

  ltable_remove(lt, cnxn_id);
}

void
gconf_listeners_notify  (GConfListeners* listeners,
                         const gchar* all_below,
                         GConfListenersCallback callback,
                         gpointer user_data)
{
  LTable* lt = (LTable*)listeners;

  ltable_notify(lt, all_below, callback, user_data);
}

/*
 * LTable impl
 */

static Listener* 
listener_new(guint cnxn_id, gpointer listener_data, GFreeFunc destroy_notify)
{
  Listener* l;

  l = g_new0(Listener, 1);

  l->listener_data = listener_data;
  l->cnxn = cnxn_id;
  l->destroy_notify = destroy_notify;

  return l;
}

static void      
listener_destroy(Listener* l)
{
  (*l->destroy_notify)(l->listener_data);
  g_free(l);
}

static LTable* 
ltable_new(void)
{
  LTable* lt;
  LTableEntry* lte;

  lt = g_new0(LTable, 1);

  lt->listeners = g_ptr_array_new();

  /* Set initial size and init error value (0) to NULL */
  g_ptr_array_set_size(lt->listeners, 5);
  g_ptr_array_index(lt->listeners, 0) = NULL;

  lte = ltable_entry_new("/");

  lt->tree = g_node_new(lte);

  lt->active_listeners = 0;

  lt->removed_cnxns = NULL;
  
  return lt;
}

static guint
ltable_next_cnxn(LTable* lt)
{
  if (lt->removed_cnxns != NULL)
    {
      guint retval = GPOINTER_TO_UINT(lt->removed_cnxns->data);

      lt->removed_cnxns = g_slist_remove(lt->removed_cnxns, lt->removed_cnxns->data);

      return retval;
    }
  else
    {
      lt->next_cnxn += 1;
      return lt->next_cnxn - 1;
    }
}

static void
ltable_insert(LTable* lt, const gchar* where, Listener* l)
{
  gchar** dirnames;
  guint i;
  GNode* cur;
  GNode* found = NULL;
  LTableEntry* lte;
  const gchar* noroot_where = where + 1;

  /* Add to the tree */
  dirnames = g_strsplit(noroot_where, "/", -1);
  
  cur = lt->tree;
  i = 0;
  while (dirnames[i])
    {
      LTableEntry* ne;
      GNode* across;

      /* Find this dirname on this level, or add it. */
      g_assert (cur != NULL);        

      found = NULL;

      across = cur->children;

      while (across != NULL)
        {
          LTableEntry* lte = across->data;
          int cmp;

          cmp = strcmp(lte->name, dirnames[i]);

          if (cmp == 0)
            {
              found = across;
              break;
            }
          else if (cmp > 0)
            {
              /* Past it */
              break;
            }
          else 
            {
              across = g_node_next_sibling(across);
            }
        }

      if (found == NULL)
        {
          ne = ltable_entry_new(dirnames[i]);
              
          if (across != NULL) /* Across is at the one past */
            found = g_node_insert_data_before(cur, across, ne);
          else                /* Never went past, append - could speed this up by saving last visited */
            found = g_node_append_data(cur, ne);
        }

      g_assert(found != NULL);

      cur = found;

      ++i;
    }

  /* cur is still the root node ("/") if where was "/" since nothing
     was returned from g_strsplit */
  lte = cur->data;

  lte->listeners = g_list_prepend(lte->listeners, l);

  g_strfreev(dirnames);

  /* Add tree node to the flat table */
  g_ptr_array_set_size(lt->listeners, lt->next_cnxn);
  g_ptr_array_index(lt->listeners, l->cnxn) = found;

  lt->active_listeners += 1;
}

static void    
ltable_remove(LTable* lt, guint cnxn)
{
  LTableEntry* lte;
  GList* tmp;
  GNode* node;

  g_return_if_fail(cnxn < lt->listeners->len);
  if (cnxn >= lt->listeners->len)
    return;
  
  /* Remove from the flat table */
  node = g_ptr_array_index(lt->listeners, cnxn);
  g_ptr_array_index(lt->listeners, cnxn) = NULL;

  g_return_if_fail(node != NULL);
  if (node == NULL)
    return;

  lte = node->data;
  
  tmp = lte->listeners;

  g_return_if_fail(tmp != NULL);

  while (tmp != NULL)
    {
      Listener* l = tmp->data;

      if (l->cnxn == cnxn)
        {
          if (tmp->prev)
            {
              tmp->prev->next = tmp->next;
            }
          else
            {
              /* tmp was the first (and maybe last) node */
              lte->listeners = tmp->next;
            }
          if (tmp->next)
            {
              tmp->next->prev = tmp->prev;
            }
          g_list_free_1(tmp);

          lt->removed_cnxns = g_slist_prepend(lt->removed_cnxns,
                                              GUINT_TO_POINTER(l->cnxn));
          
          listener_destroy(l);

          break;
        }

      tmp = g_list_next(tmp);
    }
  
  g_return_if_fail(tmp != NULL);

  /* Remove from the tree if this node is now pointless */
  if (lte->listeners == NULL && node->children == NULL)
    {
      ltable_entry_destroy(lte);
      g_node_destroy(node);
    }

  lt->active_listeners -= 1;
}

static void    
ltable_destroy(LTable* ltable)
{
  guint i;

  i = ltable->listeners->len - 1;

  while (i > 0) /* 0 position in array is invalid */
    {
      if (g_ptr_array_index(ltable->listeners, i) != NULL)
        {
          listener_destroy(g_ptr_array_index(ltable->listeners, i));
          g_ptr_array_index(ltable->listeners, i) = NULL;
        }
      
      --i;
    }
  
  g_ptr_array_free(ltable->listeners, TRUE);

  g_node_destroy(ltable->tree);

  g_slist_free(ltable->removed_cnxns);
  
  g_free(ltable);
}

static void
notify_listener_list(GConfListeners* listeners, GList* list, const gchar* key, GConfListenersCallback callback, gpointer user_data)
{
  GList* tmp;

  tmp = list;
  while (tmp != NULL)
    {
      Listener* l = tmp->data;

      (*callback)(listeners, l->cnxn, l->listener_data, user_data);

      tmp = g_list_next(tmp);
    }
}

static void    
ltable_notify(LTable* lt, const gchar* key,
              GConfListenersCallback callback, gpointer user_data)
{
  gchar** dirs;
  guint i;
  const gchar* noroot_key;
  GNode* cur;

  noroot_key = key + 1;

  g_return_if_fail(*key == '/');
  
  /* Notify "/" listeners */
  notify_listener_list((GConfListeners*)lt,
                       ((LTableEntry*)lt->tree->data)->listeners, 
                       key, callback, user_data);

  dirs = g_strsplit(noroot_key, "/", -1);

  cur = lt->tree;
  i = 0;
  while (dirs[i] && cur)
    {
      GNode* child = cur->children;

      while (child != NULL)
        {
          LTableEntry* lte = child->data;

          if (strcmp(lte->name, dirs[i]) == 0)
            {
              notify_listener_list((GConfListeners*)lt,
                                   lte->listeners, key,
                                   callback, user_data);
              break;
            }

          child = g_node_next_sibling(child);
        }

      if (child != NULL) /* we found a child, scan below it */
        cur = child;
      else               /* end of the line */
        cur = NULL;

      ++i;
    }
  
  g_strfreev(dirs);
}

static LTableEntry* 
ltable_entry_new(const gchar* name)
{
  LTableEntry* lte;

  lte = g_new0(LTableEntry, 1);

  lte->name = g_strdup(name);

  return lte;
}

static void         
ltable_entry_destroy(LTableEntry* lte)
{
  g_return_if_fail(lte->listeners == NULL); /* should destroy all listeners first. */
  g_free(lte->name);
  g_free(lte);
}

#if 0
/* Debug */
gboolean
spew_func(GNode* node, gpointer data)
{
  LTableEntry* lte = node->data;  
  GList* tmp;

  gconf_log(GCL_DEBUG, " Spewing node `%s'", lte->name);

  tmp = lte->listeners;
  while (tmp != NULL)
    {
      Listener* l = tmp->data;

      gconf_log(GCL_DEBUG, "   listener %u is here", (guint)l->cnxn);

      tmp = g_list_next(tmp);
    }

  return FALSE;
}

static void    
ltable_spew(LTable* lt)
{
  g_node_traverse(lt->tree, G_LEVEL_ORDER, G_TRAVERSE_ALL, -1, spew_func, NULL);
}
#endif
