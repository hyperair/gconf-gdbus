
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

typedef struct _GConfBackendVTable GConfBackendVTable;

struct _GConfBackendVTable {
  GConfBackend* (* init)            (void);
  void          (* shutdown)        (GConfBackend* backend);

  GConfSource*  (* resolve_address) (GConfBackend* backend, const gchar* address);

  GConfValue*   (* query_value)     (GConfSource* source, const gchar* key);
};

struct _GConfBackend {
  guint refcount;
  GConfBackendVTable* vtable;
};

/* Obtain the GConfBackend for this address, based on the first part of the 
 * address.
 */
GConfBackend* g_conf_get_backend(const gchar* address);

/* Backends start with a refcount of 0, which is a bit weird for Gtk programmers
 * but essential since you don't know from g_conf_backend_get() if you're the first
 * to obtain the backend or if you got an already-cached backend.
 */
void          g_conf_backend_ref(GConfBackend* backend);
void          g_conf_backend_unref(GConfBackend* backend);

#endif
