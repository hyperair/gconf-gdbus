
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

#ifndef GCONF_GCONF_SCHEMA_H
#define GCONF_GCONF_SCHEMA_H

#include <glib.h>

/* 
 *  A "schema" is a value that describes a key-value pair.
 *  It might include the type of the pair, documentation describing 
 *  the pair, the name of the application creating the pair, 
 *  etc.
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
  G_CONF_VALUE_PAIR 
} GConfValueType;

typedef struct _GConfSchema GConfSchema;

struct _GConfSchema {
  GConfValueType type; /* Type of the described entry */
  gchar* short_desc;   /* 40 char or less description, no newlines */
  gchar* long_desc;    /* could be a paragraph or so */
  gchar* owner;        /* Name of creating application */
};

GConfSchema*  g_conf_schema_new(void);
void          g_conf_schema_destroy(GConfSchema* sc);
GConfSchema*  g_conf_schema_copy(GConfSchema* sc);

void          g_conf_schema_set_type(GConfSchema* sc, GConfValueType type);
void          g_conf_schema_set_short_desc(GConfSchema* sc, const gchar* desc);
void          g_conf_schema_set_long_desc(GConfSchema* sc, const gchar* desc);
void          g_conf_schema_set_owner(GConfSchema* sc, const gchar* owner);

#define       g_conf_schema_type(x) (x->type)
#define       g_conf_schema_short_desc(x) ((const gchar*)(x)->short_desc)
#define       g_conf_schema_long_desc(x)  ((const gchar*)(x)->long_desc)
#define       g_conf_schema_owner(x)      ((const gchar*)(x)->owner)

#endif


