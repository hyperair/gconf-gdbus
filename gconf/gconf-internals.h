
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
  guint flags;
  gchar* address;
  GConfBackend* backend;
};

typedef enum {
  G_CONF_SOURCE_WRITEABLE = 1 << 0,
  G_CONF_SOURCE_ALL_FLAGS = ((1 << 0))
} GConfSourceFlags;

GConfSource*  g_conf_resolve_address         (const gchar* address);
GConfValue*   g_conf_source_query_value      (GConfSource* source,
                                              const gchar* key);
void          g_conf_source_set_value        (GConfSource* source,
                                              const gchar* key,
                                              GConfValue* value);
void          g_conf_source_unset_value      (GConfSource* source,
                                              const gchar* key);
GSList*      g_conf_source_all_entries         (GConfSource* source,
                                                const gchar* dir);
GSList*      g_conf_source_all_dirs          (GConfSource* source,
                                              const gchar* dir);

void         g_conf_source_set_schema        (GConfSource* source,
                                              const gchar* key,
                                              const gchar* schema_key);

void         g_conf_source_remove_dir        (GConfSource* source,
                                              const gchar* dir);
void          g_conf_source_nuke_dir        (GConfSource* source,
                                             const gchar* dir);

gboolean     g_conf_source_sync_all          (GConfSource* source);
void         g_conf_source_destroy (GConfSource* source);

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
void          fill_corba_value_from_g_conf_value(GConfValue* value, 
                                                 ConfigValue* dest);
ConfigValue*  invalid_corba_value();

void          fill_corba_schema_from_g_conf_schema(GConfSchema* sc, 
                                                   ConfigSchema* dest);
ConfigSchema* corba_schema_from_g_conf_schema(GConfSchema* sc);
GConfSchema*  g_conf_schema_from_corba_schema(const ConfigSchema* cs);

const gchar* g_conf_value_type_to_string(GConfValueType type);
GConfValueType g_conf_value_type_from_string(const gchar* str);

/* This is the actual thing we want to talk to, the stack of sources */
typedef struct _GConfSources GConfSources;

struct _GConfSources {
  GList* sources;
  
};

GConfSources* g_conf_sources_new(gchar** addresses);
void          g_conf_sources_destroy(GConfSources* sources);
GConfValue*   g_conf_sources_query_value (GConfSources* sources, 
                                          const gchar* key);
void          g_conf_sources_set_value   (GConfSources* sources,
                                          const gchar* key,
                                          GConfValue* value);
void          g_conf_sources_unset_value (GConfSources* sources,
                                          const gchar* key);
GSList*       g_conf_sources_all_entries   (GConfSources* sources,
                                            const gchar* dir);
GSList*       g_conf_sources_all_dirs   (GConfSources* sources,
                                         const gchar* dir);
void          g_conf_sources_remove_dir (GConfSources* sources,
                                         const gchar* dir);

void          g_conf_sources_nuke_dir (GConfSources* sources,
                                       const gchar* dir);

void          g_conf_sources_set_schema        (GConfSources* sources,
                                                const gchar* key,
                                                const gchar* schema_key);

gboolean      g_conf_sources_sync_all    (GConfSources* sources);

gchar**       g_conf_load_source_path(const gchar* filename);

/* shouldn't be used in applications (although implemented in gconf.c) */
void          g_conf_shutdown_daemon(void);
gboolean      g_conf_ping_daemon(void);
gboolean      g_conf_spawn_daemon(void);

gchar*        g_conf_concat_key_and_dir(const gchar* dir, const gchar* key);

const gchar*  g_conf_global_appname(void);

#endif


