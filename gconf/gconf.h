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

#ifndef GCONF_GCONF_H
#define GCONF_GCONF_H

#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "gconf-schema.h"
#include "gconf-orbit.h"
#include "gconf-engine.h"
#include "gconf-error.h"
  
gboolean     gconf_init            (GConfError** err);
gboolean     gconf_is_initialized  (void);

typedef void (*GConfNotifyFunc)(GConfEngine* conf, guint cnxn_id, const gchar* key, GConfValue* value, gpointer user_data);
  
/* Returns ID of the notification */
/* returns 0 on error, 0 is an invalid ID */
guint        gconf_notify_add(GConfEngine* conf,
                              const gchar* namespace_section, /* dir or key to listen to */
                              GConfNotifyFunc func,
                              gpointer user_data,
                              GConfError** err);

void         gconf_notify_remove  (GConfEngine* conf,
                                   guint cnxn);


/* Low-level interfaces */
GConfValue*  gconf_get            (GConfEngine* conf, const gchar* key, GConfError** err);

gboolean     gconf_set            (GConfEngine* conf, const gchar* key,
                                   GConfValue* value, GConfError** err);

gboolean     gconf_unset          (GConfEngine* conf, const gchar* key, GConfError** err);

GSList*      gconf_all_entries    (GConfEngine* conf, const gchar* dir, GConfError** err);

GSList*      gconf_all_dirs       (GConfEngine* conf, const gchar* dir, GConfError** err);

void         gconf_suggest_sync   (GConfEngine* conf, GConfError** err);

gboolean     gconf_dir_exists     (GConfEngine* conf, const gchar* dir, GConfError** err);

/* if you pass non-NULL for why_invalid, it gives a user-readable
   explanation of the problem in g_malloc()'d memory
*/
gboolean     gconf_valid_key      (const gchar* key, gchar** why_invalid);

/* return TRUE if the path "below" would be somewhere below the directory "above" */
gboolean     gconf_key_is_below   (const gchar* above, const gchar* below);

/* 
 * Higher-level stuff 
 */

/* 'def' (default) is used if the key is not set or if there's an error. */

gdouble      gconf_get_float (GConfEngine* conf, const gchar* key,
                              gdouble def, GConfError** err);

gint         gconf_get_int   (GConfEngine* conf, const gchar* key,
                              gint def, GConfError** err);

/* free the retval */
gchar*       gconf_get_string(GConfEngine* conf, const gchar* key,
                              const gchar* def, /* def is copied when returned, 
                                                 * and can be NULL to return 
                                                 * NULL 
                                                 */
                              GConfError** err);

gboolean     gconf_get_bool  (GConfEngine* conf, const gchar* key,
                              gboolean def, GConfError** err);

/* this one has no default since it would be expensive and make little
   sense; it returns NULL as a default, to indicate unset or error */
/* free the retval */
/* Note that this returns the schema stored at key, NOT
   the schema that key conforms to. */
GConfSchema* gconf_get_schema  (GConfEngine* conf, const gchar* key, GConfError** err);

/* No convenience functions for lists or pairs, since there are too
   many combinations of types possible
*/

/* setters return TRUE on success; note that you still should suggest a sync */

gboolean     gconf_set_float   (GConfEngine* conf, const gchar* key,
                                gdouble val, GConfError** err);

gboolean     gconf_set_int     (GConfEngine* conf, const gchar* key,
                                gint val, GConfError** err);

gboolean     gconf_set_string  (GConfEngine* conf, const gchar* key,
                                const gchar* val, GConfError** err);

gboolean     gconf_set_bool    (GConfEngine* conf, const gchar* key,
                                gboolean val, GConfError** err);

gboolean     gconf_set_schema  (GConfEngine* conf, const gchar* key,
                                GConfSchema* val, GConfError** err);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif



