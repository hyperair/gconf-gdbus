/* GConf
 * Copyright (C) 1999, 2000 Red Hat Inc.
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

#ifndef GCONF_GCONF_ENGINE_H
#define GCONF_GCONF_ENGINE_H

#include <glib.h>
#include <gconf/gconf-error.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* A configuration engine (stack of config sources); normally there's
 * just one of these on the system.  
 */
typedef struct _GConfEngine GConfEngine;
  
GConfEngine* gconf_engine_get_default     (void);
/* returns NULL on error; requests single specified source */
GConfEngine* gconf_engine_get_for_address (const gchar* address,
                                           GError** err);
void         gconf_engine_unref           (GConfEngine* conf);
void         gconf_engine_ref             (GConfEngine* conf);

#ifdef GCONF_ENABLE_INTERNALS
GConfEngine* gconf_engine_get_local      (const gchar* address, GError** err);
#endif

/* For use by language bindings only, will be deprecated in GNOME 2.0
 * when we can make GConfEngine a GObject
 */
void         gconf_engine_set_user_data  (GConfEngine   *engine,
                                          gpointer       data,
                                          GDestroyNotify dnotify);
gpointer     gconf_engine_get_user_data  (GConfEngine   *engine);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif



