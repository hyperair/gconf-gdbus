
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

#include "gconf-schema.h"

/* Errors */

typedef enum {
  G_CONF_SUCCESS = 0,
  G_CONF_FAILED = 1,        /* Something didn't work, don't know why, probably unrecoverable
                               so there's no point having a more specific errno */

  G_CONF_NO_SERVER = 2,     /* Server can't be launched/contacted */
  G_CONF_NO_PERMISSION = 3, /* don't have permission for that */
  G_CONF_BAD_ADDRESS = 4,   /* Address couldn't be resolved */
  G_CONF_BAD_KEY = 5,       /* directory or key isn't valid (contains bad
                               characters, or malformed slash arrangement) */
  G_CONF_PARSE_ERROR = 6,   /* Syntax error when parsing */
  G_CONF_CORRUPT = 7,       /* Error parsing/loading information inside the backend */
  G_CONF_TYPE_MISMATCH = 8, /* Type requested doesn't match type found */
  G_CONF_IS_DIR = 9,        /* Requested key operation on a dir */
  G_CONF_IS_KEY = 10        /* Requested dir operation on a key */
} GConfErrNo;

const gchar* g_conf_error          (void); /* returns strerror of current errno,
                                              combined with additional details 
                                              that may exist 
                                           */
const gchar* g_conf_strerror       (GConfErrNo en);
GConfErrNo   g_conf_errno          (void);
void         g_conf_set_error      (GConfErrNo en, const gchar* format, ...) G_GNUC_PRINTF (2, 3);
void         g_conf_clear_error    (void); /* like setting errno to 0 */


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

gchar*      g_conf_value_to_string(GConfValue* value);

typedef struct _GConfEntry GConfEntry;

struct _GConfEntry {
  gchar* key;
  GConfValue* value;
};

/* Pair takes memory ownership of both key and value */
GConfEntry* g_conf_entry_new    (gchar* key, GConfValue* val);
void        g_conf_entry_destroy(GConfEntry* pair);

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

/* Returns ID of the notification */
guint        g_conf_notify_add(GConf* conf,
                               const gchar* namespace_section, /* dir or key to listen to */
                               GConfNotifyFunc func,
                               gpointer user_data);

void         g_conf_notify_remove(GConf* conf,
                                  guint cnxn);


/* Low-level interfaces */
GConfValue*  g_conf_get(GConf* conf, const gchar* key);

void         g_conf_set(GConf* conf, const gchar* key, GConfValue* value);

void         g_conf_unset(GConf* conf, const gchar* key);

GSList*      g_conf_all_entries(GConf* conf, const gchar* dir);

GSList*      g_conf_all_dirs(GConf* conf, const gchar* dir);

void         g_conf_sync(GConf* conf);

gboolean     g_conf_valid_key      (const gchar* key);

/* 
 * Higher-level stuff 
 */

/* 'def' (default) is used if the key is not set or if there's an error. */

gdouble      g_conf_get_float (GConf* conf, const gchar* key,
                               gdouble def);

gint         g_conf_get_int   (GConf* conf, const gchar* key,
                               gint def);

/* free the retval */
gchar*       g_conf_get_string(GConf* conf, const gchar* key,
                               const gchar* def); /* def is copied when returned, 
                                                   * and can be NULL to return 
                                                   * NULL 
                                                   */

gboolean     g_conf_get_bool  (GConf* conf, const gchar* key,
                               gboolean def);

/* this one has no default since it would be expensive and make little
   sense; it returns NULL as a default, to indicate unset or error */
/* free the retval */
GConfSchema* g_conf_get_schema  (GConf* conf, const gchar* key);




#endif



