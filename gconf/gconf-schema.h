
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

#include "gconf-value.h"

/* 
 *  A "schema" is a value that describes a key-value pair.
 *  It might include the type of the pair, documentation describing 
 *  the pair, the name of the application creating the pair, 
 *  etc.
 */

struct _GConfSchema {
  GConfValueType type; /* Type of the described entry */
  gchar* short_desc;   /* 40 char or less description, no newlines */
  gchar* long_desc;    /* could be a paragraph or so */
  gchar* owner;        /* Name of creating application */
  GConfValue* default_value; /* Default value of the key */
};

GConfSchema*  gconf_schema_new(void);
void          gconf_schema_destroy(GConfSchema* sc);
GConfSchema*  gconf_schema_copy(GConfSchema* sc);

void          gconf_schema_set_type(GConfSchema* sc, GConfValueType type);
void          gconf_schema_set_short_desc(GConfSchema* sc, const gchar* desc);
void          gconf_schema_set_long_desc(GConfSchema* sc, const gchar* desc);
void          gconf_schema_set_owner(GConfSchema* sc, const gchar* owner);
void          gconf_schema_set_default_value(GConfSchema* sc, GConfValue* val);
void          gconf_schema_set_default_value_nocopy(GConfSchema* sc, GConfValue* val);

#define       gconf_schema_type(x) (x->type)
#define       gconf_schema_short_desc(x) ((const gchar*)(x)->short_desc)
#define       gconf_schema_long_desc(x)  ((const gchar*)(x)->long_desc)
#define       gconf_schema_owner(x)      ((const gchar*)(x)->owner)
#define       gconf_schema_default_value(x) ((x)->default_value)

#endif


