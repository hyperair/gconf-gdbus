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
#include "gconf-conf.h"
#include "gconf-error.h"
  
gboolean     g_conf_init            (void);
gboolean     g_conf_is_initialized  (void);

typedef void (*GConfNotifyFunc)(GConf* conf, guint cnxn_id, const gchar* key, GConfValue* value, gpointer user_data);
  
/* Returns ID of the notification */
guint        g_conf_notify_add(GConf* conf,
                               const gchar* namespace_section, /* dir or key to listen to */
                               GConfNotifyFunc func,
                               gpointer user_data);

void         g_conf_notify_remove  (GConf* conf,
                                    guint cnxn);


/* Low-level interfaces */
GConfValue*  g_conf_get            (GConf* conf, const gchar* key);

void         g_conf_set            (GConf* conf, const gchar* key, GConfValue* value);

void         g_conf_unset          (GConf* conf, const gchar* key);

GSList*      g_conf_all_entries    (GConf* conf, const gchar* dir);

GSList*      g_conf_all_dirs       (GConf* conf, const gchar* dir);

void         g_conf_sync           (GConf* conf);

gboolean     g_conf_dir_exists     (GConf *conf, const gchar* dir);

gboolean     g_conf_valid_key      (const gchar* key);


/* 
 * Higher-level stuff 
 */

/* 'def' (default) is used if the key is not set or if there's an error. */

gdouble      g_conf_get_float (GConf* conf, const gchar* key,
                               gdouble def);

gint         g_conf_get_int   (GConf* conf, const gchar* key,
                               gint def);

/* free the retval */
gchar*       g_conf_get_string(GConf* conf, const gchar* key,
                               const gchar* def); /* def is copied when returned, 
                                                   * and can be NULL to return 
                                                   * NULL 
                                                   */

gboolean     g_conf_get_bool  (GConf* conf, const gchar* key,
                               gboolean def);

/* this one has no default since it would be expensive and make little
   sense; it returns NULL as a default, to indicate unset or error */
/* free the retval */
/* Note that this returns the schema stored at key, NOT
   the schema that key conforms to. */
GConfSchema* g_conf_get_schema  (GConf* conf, const gchar* key);

/* No convenience functions for lists or pairs, since there are too
   many combinations of types possible
*/

/* setters return TRUE on success; note that you still have to sync */

gboolean     g_conf_set_float   (GConf* conf, const gchar* key,
                                 gdouble val);

gboolean     g_conf_set_int     (GConf* conf, const gchar* key,
                                 gint val);

gboolean     g_conf_set_string  (GConf* conf, const gchar* key,
                                 const gchar* val);

gboolean     g_conf_set_bool    (GConf* conf, const gchar* key,
                                 gboolean val);

gboolean     g_conf_set_schema  (GConf* conf, const gchar* key,
                                 GConfSchema* val);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif



