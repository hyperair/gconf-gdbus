
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

#ifndef GCONF_GCONF_VALUE_H
#define GCONF_GCONF_VALUE_H

#include <glib.h>

/* 
 * A GConfValue is used to pass configuration values around
 */

typedef enum {
  G_CONF_VALUE_INVALID,
  G_CONF_VALUE_STRING,
  G_CONF_VALUE_INT,
  G_CONF_VALUE_FLOAT,
  G_CONF_VALUE_BOOL,
  G_CONF_VALUE_SCHEMA,

  /* unfortunately these aren't really types; we want list_of_string,
     list_of_int, etc.  but it's just too complicated to implement.
     instead we'll complain in various places if you do something
     moronic like mix types in a list or treat pair<string,int> and
     pair<float,bool> as the same type. */
  G_CONF_VALUE_LIST,
  G_CONF_VALUE_PAIR,

  /* This is special magic used internally only */
  /* It indicates that the search for a value should end with this
     source, not progressing further to later sources in the path.
     If there's a default value in the schema, that will be used.
  */
  G_CONF_VALUE_IGNORE_SUBSEQUENT
  
} GConfValueType;

/* Forward declaration */
typedef struct _GConfSchema GConfSchema;

typedef struct _GConfValue GConfValue;

struct _GConfValue {
  GConfValueType type;
  union {
    gchar* string_data;
    gint int_data;
    gboolean bool_data;
    gdouble float_data;
    GConfSchema* schema_data;
    struct {
      GConfValueType type;
      GSList* list;
    } list_data;
    struct {
      GConfValue* car;
      GConfValue* cdr;
    } pair_data;
  } d;
};

#define g_conf_value_string(x)    ((x)->d.string_data)
#define g_conf_value_int(x)       ((x)->d.int_data)
#define g_conf_value_float(x)     ((x)->d.float_data)
#define g_conf_value_list_type(x) ((x)->d.list_data.type)
#define g_conf_value_list(x)      ((x)->d.list_data.list)
#define g_conf_value_car(x)       ((x)->d.pair_data.car)
#define g_conf_value_cdr(x)       ((x)->d.pair_data.cdr)
#define g_conf_value_bool(x)      ((x)->d.bool_data)
#define g_conf_value_schema(x)    ((x)->d.schema_data)

GConfValue* g_conf_value_new(GConfValueType type);
/* doesn't work on complicated types (only string, int, bool, float) */
GConfValue* g_conf_value_new_from_string(GConfValueType type, const gchar* str);
/* for the complicated types */
GConfValue* g_conf_value_new_list_from_string(GConfValueType list_type,
                                              const gchar* str);
GConfValue* g_conf_value_new_pair_from_string(GConfValueType car_type,
                                              GConfValueType cdr_type,
                                              const gchar* str);
GConfValue* g_conf_value_copy(GConfValue* src);
void        g_conf_value_destroy(GConfValue* value);

void        g_conf_value_set_int(GConfValue* value, gint the_int);
void        g_conf_value_set_string(GConfValue* value, const gchar* the_str);
void        g_conf_value_set_float(GConfValue* value, gdouble the_float);
void        g_conf_value_set_bool(GConfValue* value, gboolean the_bool);
void        g_conf_value_set_schema(GConfValue* value, GConfSchema* sc);
void        g_conf_value_set_schema_nocopy(GConfValue* value, GConfSchema* sc);
void        g_conf_value_set_car(GConfValue* value, GConfValue* car);
void        g_conf_value_set_car_nocopy(GConfValue* value, GConfValue* car);
void        g_conf_value_set_cdr(GConfValue* value, GConfValue* cdr);
void        g_conf_value_set_cdr_nocopy(GConfValue* value, GConfValue* cdr);
/* Set a list of GConfValue, NOT lists or pairs */
void        g_conf_value_set_list_type(GConfValue* value,
                                       GConfValueType type);
void        g_conf_value_set_list_nocopy(GConfValue* value,
                                         GSList* list);

gchar*      g_conf_value_to_string(GConfValue* value);


#endif


