
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

#ifndef GCONF_GCONF_H
#define GCONF_GCONF_H

#include <glib.h>

typedef enum {
  G_CONF_VALUE_INVALID,
  G_CONF_VALUE_STRING,
  G_CONF_VALUE_INT,
  G_CONF_VALUE_FLOAT,
  G_CONF_VALUE_BOOL,
  G_CONF_VALUE_LIST_OF_STRING,
  G_CONF_VALUE_LIST_OF_INT,
  G_CONF_VALUE_LIST_OF_FLOAT,
  G_CONF_VALUE_LIST_OF_BOOL
} GConfValueType;

/* 
 * A GConfValue is used to pass configuration values around
 */

typedef struct _GConfValue GConfValue;

struct _GConfValue {
  GConfValueType type;
  union {
    gchar* string_data;
    gint int_data;
    gboolean bool_data;
    gdouble float_data;
    GSList* list_data;
  } d;
};

#define g_conf_value_string(x) ((x)->d.string_data)
#define g_conf_value_int(x)    ((x)->d.int_data)
#define g_conf_value_float(x)  ((x)->d.float_data)
#define g_conf_value_list(x)   ((x)->d.list_data)
#define g_conf_value_bool(x)   ((x)->d.bool_data)

GConfValue* g_conf_value_new(GConfValueType type);
GConfValue* g_conf_value_new_from_string(GConfValueType type, const gchar* str);
GConfValue* g_conf_value_copy(GConfValue* src);
void        g_conf_value_destroy(GConfValue* value);

void        g_conf_value_set_int(GConfValue* value, gint the_int);
void        g_conf_value_set_string(GConfValue* value, const gchar* the_str);
void        g_conf_value_set_float(GConfValue* value, gdouble the_float);
void        g_conf_value_set_bool(GConfValue* value, gboolean the_bool);

gchar*      g_conf_value_to_string(GConfValue* value);

typedef struct _GConfPair GConfPair;

struct _GConfPair {
  gchar* key;
  GConfValue* value;
};

/* Pair takes memory ownership of both key and value */
GConfPair* g_conf_pair_new(gchar* key, GConfValue* val);
void       g_conf_pair_destroy(GConfPair* pair);

/* A configuration engine (stack of config sources); normally there's
 * just one of these on the system.  
 */
typedef struct _GConf GConf;

struct _GConf {
  gpointer dummy;
};

typedef void (*GConfNotifyFunc)(GConf* conf, guint cnxn_id, const gchar* key, GConfValue* value, gpointer user_data);

gboolean     g_conf_init           ();

GConf*       g_conf_new            (void);
void         g_conf_destroy        (GConf* conf);

const gchar* g_conf_error          (void);
gboolean     g_conf_error_pending  (void);
void         g_conf_set_error      (const gchar* str);


/* Returns ID of the notification */
guint        g_conf_notify_add(GConf* conf,
                               const gchar* namespace_section, /* dir or key to listen to */
                               GConfNotifyFunc func,
                               gpointer user_data);

void         g_conf_notify_remove(GConf* conf,
                                  guint cnxn);


/* We'll have higher-level versions that return a double or string instead of a GConfValue */
GConfValue*  g_conf_get(GConf* conf, const gchar* key);

/* ditto, higher-level version planned. */
void         g_conf_set(GConf* conf, const gchar* key, GConfValue* value);

GSList*      g_conf_all_pairs(GConf* conf, const gchar* dir);

void         g_conf_sync(GConf* conf);

gboolean     g_conf_valid_key      (const gchar* key);

#endif


