
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

#ifndef GCONF_GCONFBACKEND_H
#define GCONF_GCONFBACKEND_H

#include <gconf/gconf-internals.h>
#include <gmodule.h>

typedef struct _GConfBackendVTable GConfBackendVTable;

struct _GConfBackendVTable {
  void                (* shutdown)        (void);

  GConfSource*        (* resolve_address) (const gchar* address);

  GConfValue*         (* query_value)     (GConfSource* source, 
                                           const gchar* key);

  void                (* set_value)       (GConfSource* source, 
                                           const gchar* key, 
                                           GConfValue* value);

  /* Returns list of GConfEntry */
  GSList*             (* all_entries)     (GConfSource* source,
                                           const gchar* dir);

  /* Returns list of allocated strings */
  GSList*             (* all_subdirs)     (GConfSource* source,
                                           const gchar* dir);

  void                (* unset_value)     (GConfSource* source,
                                           const gchar* key);

  void                (* remove_dir)      (GConfSource* source,
                                           const gchar* dir);

  void                (* nuke_dir)      (GConfSource* source,
                                         const gchar* dir);

  gboolean            (* sync_all)        (GConfSource* source);

  void                (* destroy_source)  (GConfSource* source);
};

struct _GConfBackend {
  const gchar* name;
  guint refcount;
  GConfBackendVTable* vtable;
  GModule* module;
};

/* Address utility stuff */

/* Get the backend name */
gchar* g_conf_address_backend(const gchar* address);
/* Get the resource name understood only by the backend */
gchar* g_conf_address_resource(const gchar* address);

gchar*       g_conf_backend_file(const gchar* address);

/* Obtain the GConfBackend for this address, based on the first part of the 
 * address. The refcount is always incremented, and you must unref() later
 */
GConfBackend* g_conf_get_backend(const gchar* address);

void          g_conf_backend_ref(GConfBackend* backend);
void          g_conf_backend_unref(GConfBackend* backend);

GConfSource*  g_conf_backend_resolve_address (GConfBackend* backend, 
                                              const gchar* address);

#endif

