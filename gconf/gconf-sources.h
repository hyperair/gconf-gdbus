
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
  /* These are an optimization to avoid calls to
   * the writeable/readable methods in the backend
   * vtable
   */
  GCONF_SOURCE_ALL_WRITEABLE = 1 << 0,
  GCONF_SOURCE_ALL_READABLE = 1 << 1,
  GCONF_SOURCE_ALL_FLAGS = ((1 << 0) | (1 << 1))
} GConfSourceFlags;

GConfSource*  gconf_resolve_address         (const gchar* address,
                                             GConfError** err);

void          gconf_source_destroy          (GConfSource* source);

/* This is the actual thing we want to talk to, the stack of sources */
typedef struct _GConfSources GConfSources;

struct _GConfSources {
  GList* sources;
  
};

/* Even on error, this gives you an empty source list, i.e.  never
   returns NULL but may set the error if some addresses weren't
   resolved and may contain no sources.  */
GConfSources* gconf_sources_new_from_addresses    (gchar** addresses,
                                                   GConfError** err);

void          gconf_sources_destroy     (GConfSources* sources);

GConfValue*   gconf_sources_query_value (GConfSources* sources, 
                                         const gchar* key,
                                         GConfError** err);

void          gconf_sources_set_value   (GConfSources* sources,
                                         const gchar* key,
                                         GConfValue* value,
                                         GConfError** err);

void          gconf_sources_unset_value (GConfSources* sources,
                                         const gchar* key,
                                         GConfError** err);

GSList*       gconf_sources_all_entries (GConfSources* sources,
                                         const gchar* dir,
                                         GConfError** err);

GSList*       gconf_sources_all_dirs    (GConfSources* sources,
                                         const gchar* dir,
                                         GConfError** err);

gboolean      gconf_sources_dir_exists  (GConfSources* sources,
                                         const gchar* dir,
                                         GConfError** err);

void          gconf_sources_remove_dir  (GConfSources* sources,
                                         const gchar* dir,
                                         GConfError** err);

void          gconf_sources_set_schema  (GConfSources* sources,
                                         const gchar* key,
                                         const gchar* schema_key,
                                         GConfError** err);

gboolean      gconf_sources_sync_all    (GConfSources* sources,
                                         GConfError** err);

#endif


