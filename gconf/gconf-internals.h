
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

#ifndef GCONF_GCONF_INTERNALS_H
#define GCONF_GCONF_INTERNALS_H

#include <glib.h>
#include "gconf-error.h"
#include "gconf-value.h"
#include "GConf.h"

gchar*       g_conf_key_directory  (const gchar* key);
const gchar* g_conf_key_key        (const gchar* key);

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
gchar*   g_conf_read_server_ior(GConfError** err);

GConfValue* g_conf_value_from_corba_value(const ConfigValue* value);
ConfigValue*  corba_value_from_g_conf_value(GConfValue* value);
void          fill_corba_value_from_g_conf_value(GConfValue* value, 
                                                 ConfigValue* dest);
ConfigValue*  invalid_corba_value();

void          fill_corba_schema_from_g_conf_schema(GConfSchema* sc, 
                                                   ConfigSchema* dest);
ConfigSchema* corba_schema_from_g_conf_schema(GConfSchema* sc);
GConfSchema*  g_conf_schema_from_corba_schema(const ConfigSchema* cs);

const gchar* g_conf_value_type_to_string(GConfValueType type);
GConfValueType g_conf_value_type_from_string(const gchar* str);

gchar**       g_conf_load_source_path(const gchar* filename, GConfError** err);

/* shouldn't be used in applications (although implemented in gconf.c) */
void          g_conf_shutdown_daemon(GConfError** err);
gboolean      g_conf_ping_daemon(void);
gboolean      g_conf_spawn_daemon(GConfError** err);

gchar*        g_conf_concat_key_and_dir(const gchar* dir, const gchar* key);

/* Returns 0 on failure */
gulong        g_conf_string_to_gulong(const gchar* str);

/* Log wrapper; we might want to not use syslog someday */
typedef enum {
  GCL_EMERG,
  GCL_ALERT,
  GCL_CRIT,
  GCL_ERR,
  GCL_WARNING,
  GCL_NOTICE,
  GCL_INFO,
  GCL_DEBUG
} GConfLogPriority;

void          g_conf_log      (GConfLogPriority pri, const gchar* format, ...) G_GNUC_PRINTF (2, 3);

/* return FALSE and set error if the key is bad */
gboolean      g_conf_key_check(const gchar* key, GConfError** err);

#endif


