
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

#ifndef GCONF_GCONF_VALUE_H
#define GCONF_GCONF_VALUE_H

#include <glib.h>
#include "gconf-error.h"

/* 
 * A GConfValue is used to pass configuration values around
 */

typedef enum {
  GCONF_VALUE_INVALID,
  GCONF_VALUE_STRING,
  GCONF_VALUE_INT,
  GCONF_VALUE_FLOAT,
  GCONF_VALUE_BOOL,
  GCONF_VALUE_SCHEMA,

  /* unfortunately these aren't really types; we want list_of_string,
     list_of_int, etc.  but it's just too complicated to implement.
     instead we'll complain in various places if you do something
     moronic like mix types in a list or treat pair<string,int> and
     pair<float,bool> as the same type. */
  GCONF_VALUE_LIST,
  GCONF_VALUE_PAIR
  
} GConfValueType;

#define GCONF_VALUE_TYPE_VALID(x) (((x) > GCONF_VALUE_INVALID) && ((x) <= GCONF_VALUE_PAIR))

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

#define gconf_value_get_string(x)    ((const gchar*)((x)->d.string_data))
#define gconf_value_get_int(x)       ((x)->d.int_data)
#define gconf_value_get_float(x)     ((x)->d.float_data)
#define gconf_value_get_list_type(x) ((x)->d.list_data.type)
#define gconf_value_get_list(x)      ((x)->d.list_data.list)
#define gconf_value_get_car(x)       ((x)->d.pair_data.car)
#define gconf_value_get_cdr(x)       ((x)->d.pair_data.cdr)
#define gconf_value_get_bool(x)      ((x)->d.bool_data)
#define gconf_value_get_schema(x)    ((x)->d.schema_data)

GConfValue* gconf_value_new                  (GConfValueType type);

GConfValue* gconf_value_copy                 (GConfValue* src);
void        gconf_value_free                 (GConfValue* value);

void        gconf_value_set_int              (GConfValue* value,
                                              gint the_int);
void        gconf_value_set_string           (GConfValue* value,
                                              const gchar* the_str);
void        gconf_value_set_float            (GConfValue* value,
                                              gdouble the_float);
void        gconf_value_set_bool             (GConfValue* value,
                                              gboolean the_bool);
void        gconf_value_set_schema           (GConfValue* value,
                                              GConfSchema* sc);
void        gconf_value_set_schema_nocopy    (GConfValue* value,
                                              GConfSchema* sc);
void        gconf_value_set_car              (GConfValue* value,
                                              GConfValue* car);
void        gconf_value_set_car_nocopy       (GConfValue* value,
                                              GConfValue* car);
void        gconf_value_set_cdr              (GConfValue* value,
                                              GConfValue* cdr);
void        gconf_value_set_cdr_nocopy       (GConfValue* value,
                                              GConfValue* cdr);
/* Set a list of GConfValue, NOT lists or pairs */
void        gconf_value_set_list_type        (GConfValue* value,
                                              GConfValueType type);
void        gconf_value_set_list_nocopy      (GConfValue* value,
                                              GSList* list);
void        gconf_value_set_list             (GConfValue* value,
                                              GSList* list);

gchar*      gconf_value_to_string            (GConfValue* value);

/* Meta-information about a key. Not the same as a schema; this is
 * information stored on the key, the schema is a specification
 * that may apply to this key.
 */

typedef struct _GConfMetaInfo GConfMetaInfo;

struct _GConfMetaInfo {
  gchar* schema;
  gchar* mod_user; /* user owning the daemon that made the last modification */
  GTime  mod_time; /* time of the modification */
};

#define gconf_meta_info_get_schema(x)    ((const gchar*)(x)->schema)
#define gconf_meta_info_get_mod_user(x)  ((x)->mod_user)
#define gconf_meta_info_mod_time(x)  ((x)->mod_time)

GConfMetaInfo* gconf_meta_info_new         (void);
void           gconf_meta_info_free     (GConfMetaInfo* gcmi);
void           gconf_meta_info_set_schema  (GConfMetaInfo* gcmi,
                                            const gchar* schema_name);
void           gconf_meta_info_set_mod_user(GConfMetaInfo* gcmi,
                                            const gchar* mod_user);
void           gconf_meta_info_set_mod_time(GConfMetaInfo* gcmi,
                                            GTime mod_time);


/* Key-value pairs; used to list the contents of
 *  a directory
 */  

typedef struct _GConfEntry GConfEntry;

struct _GConfEntry {
  gchar* key;
  GConfValue* value;
  gchar* schema_name;
  guint is_default : 1;
  guint is_writable : 1;
};

#define     gconf_entry_get_key(x)         ((const gchar*)(x)->key)
#define     gconf_entry_get_value(x)       ((x)->value)
#define     gconf_entry_get_schema_name(x) ((const gchar*)(x)->schema_name)
#define     gconf_entry_get_is_default(x)  ((x)->is_default)
#define     gconf_entry_get_is_writable(x) ((x)->is_writable)

GConfEntry* gconf_entry_new              (const gchar *key,
                                          GConfValue  *val);
GConfEntry* gconf_entry_new_nocopy       (gchar       *key,
                                          GConfValue  *val);
void        gconf_entry_free             (GConfEntry  *entry);

/* Transfer ownership of value to the caller. */
GConfValue* gconf_entry_steal_value      (GConfEntry  *entry);
void        gconf_entry_set_value        (GConfEntry  *entry,
                                          GConfValue  *val);
void        gconf_entry_set_value_nocopy (GConfEntry  *entry,
                                          GConfValue  *val);
void        gconf_entry_set_schema_name  (GConfEntry  *entry,
                                          const gchar *name);
void        gconf_entry_set_is_default   (GConfEntry  *entry,
                                          gboolean     is_default);
void        gconf_entry_set_is_writable  (GConfEntry  *entry,
                                          gboolean     is_writable);

#endif


