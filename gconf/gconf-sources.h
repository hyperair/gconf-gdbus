
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

#ifndef GCONF_GCONF_SOURCES_H
#define GCONF_GCONF_SOURCES_H

#include <glib.h>
#include "gconf-error.h"
#include "gconf-value.h"

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

GConfSource*  gconf_resolve_address         (const gchar* address);
GConfValue*   gconf_source_query_value      (GConfSource* source,
                                              const gchar* key,
                                              gchar** schema_name);
void          gconf_source_set_value        (GConfSource* source,
                                              const gchar* key,
                                              GConfValue* value);
void          gconf_source_unset_value      (GConfSource* source,
                                              const gchar* key);
GSList*      gconf_source_all_entries         (GConfSource* source,
                                                const gchar* dir);
GSList*      gconf_source_all_dirs          (GConfSource* source,
                                              const gchar* dir);

void         gconf_source_set_schema        (GConfSource* source,
                                              const gchar* key,
                                              const gchar* schema_key);

gboolean     gconf_source_dir_exists        (GConfSource* source,
                                              const gchar* dir);
void         gconf_source_remove_dir        (GConfSource* source,
                                              const gchar* dir);

gboolean     gconf_source_sync_all          (GConfSource* source);
void         gconf_source_destroy (GConfSource* source);

/* This is the actual thing we want to talk to, the stack of sources */
typedef struct _GConfSources GConfSources;

struct _GConfSources {
  GList* sources;
  
};

GConfSources* gconf_sources_new(gchar** addresses);
void          gconf_sources_destroy(GConfSources* sources);
GConfValue*   gconf_sources_query_value (GConfSources* sources, 
                                          const gchar* key);
void          gconf_sources_set_value   (GConfSources* sources,
                                          const gchar* key,
                                          GConfValue* value);
void          gconf_sources_unset_value (GConfSources* sources,
                                          const gchar* key);
GSList*       gconf_sources_all_entries   (GConfSources* sources,
                                            const gchar* dir);
GSList*       gconf_sources_all_dirs   (GConfSources* sources,
                                         const gchar* dir);
gboolean      gconf_sources_dir_exists (GConfSources* sources,
                                         const gchar* dir);
void          gconf_sources_remove_dir (GConfSources* sources,
                                         const gchar* dir);

void          gconf_sources_set_schema        (GConfSources* sources,
                                                const gchar* key,
                                                const gchar* schema_key);

gboolean      gconf_sources_sync_all    (GConfSources* sources);

#endif


