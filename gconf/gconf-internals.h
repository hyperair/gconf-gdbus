
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
#include "gconf.h"
#include "GConf.h"

/* Sources are not interchangeable; different backend engines will return 
 * GConfSource with different private elements.
 */

typedef struct _GConfBackend GConfBackend;

typedef struct _GConfSource GConfSource;

struct _GConfSource {
  GConfBackend* backend;
};

GConfSource* g_conf_resolve_address(const gchar* address);
GConfValue*   g_conf_source_query_value      (GConfSource* source,
                                              const gchar* key);
void          g_conf_source_set_value        (GConfSource* source,
                                              const gchar* key,
                                              GConfValue* value);
gboolean      g_conf_source_sync_all         (GConfSource* source);
void         g_conf_source_destroy (GConfSource* source);

gboolean     g_conf_valid_key      (const gchar* key);

gchar*       g_conf_key_directory  (const gchar* key);
gchar*       g_conf_key_key        (const gchar* key);

/* These file tests are in libgnome, I cut-and-pasted them */
enum {
  G_CONF_FILE_EXISTS=(1<<0)|(1<<1)|(1<<2), /*any type of file*/
  G_CONF_FILE_ISFILE=1<<0,
  G_CONF_FILE_ISLINK=1<<1,
  G_CONF_FILE_ISDIR=1<<2
};

gboolean g_conf_file_test   (const gchar* filename, int test);
gboolean g_conf_file_exists (const gchar* filename);

gchar*   g_conf_server_info_file(void);
gchar*   g_conf_server_info_dir(void);
gchar*   g_conf_read_server_ior(void);

GConfValue* g_conf_value_from_corba_value(const ConfigValue* value);
ConfigValue*  corba_value_from_g_conf_value(GConfValue* value);

#endif

