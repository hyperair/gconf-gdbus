
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

#ifndef GCONF_GCONFINTERNALS_H
#define GCONF_GCONFINTERNALS_H

#include <glib.h>

typedef enum {
  G_CONF_VALUE_INVALID,
  G_CONF_VALUE_STRING,
  G_CONF_VALUE_INT,
  G_CONF_VALUE_FLOAT,
  G_CONF_VALUE_LIST_OF_STRING,
  G_CONF_VALUE_LIST_OF_INT,
  G_CONF_VALUE_LIST_OF_FLOAT
} GConfValueType;

/* 
 * A GConfValue is used to pass configuration values around; it's 
 * pretty low-level, higher-level interface in gconf.h.
 */

typedef struct _GConfValue GConfValue;

struct _GConfValue {
  GConfValueType type;
  union {
    gchar* string_data;
    gint int_data;
    gdouble float_data;
    GSList* list_data;
  } d;
};

#define g_conf_value_string(x) ((x)->d.string_data)
#define g_conf_value_int(x)    ((x)->d.int_data)
#define g_conf_value_float(x)  ((x)->d.float_data)
#define g_conf_value_list(x)   ((x)->d.list_data)

GConfValue* g_conf_value_new(GConfValueType type);
void        g_conf_value_destroy(GConfValue* value);

void        g_conf_value_set_int(GConfValue* value, gint the_int);
void        g_conf_value_set_string(GConfValue* value, const gchar* the_str);
void        g_conf_value_set_float(GConfValue* value, gdouble the_float);

/* Sources are not interchangeable; different backend engines will return 
 * GConfSource with different private elements.
 */

typedef struct _GConfBackend GConfBackend;

typedef struct _GConfSource GConfSource;

struct _GConfSource {
  gchar* backend_id;
  GConfBackend* backend;
};

GConfSource* g_conf_resolve_address(const gchar* address);
GConfValue*   g_conf_source_query_value      (GConfSource* source,
                                              const gchar* key);
void         g_conf_source_destroy (GConfSource* source);

const gchar* g_conf_error          (void);
gboolean     g_conf_error_pending  (void);
void         g_conf_set_error      (const gchar* str);

#endif
