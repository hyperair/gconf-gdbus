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
#include "gconf-engine.h"
#include "gconf-error.h"
  
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

/* Locale only matters if you are expecting to get a schema, or if you
   don't know what you are expecting and it might be a schema. Note
   that gconf_get() automatically uses the current locale, which is
   normally what you want. */
GConfValue*  gconf_get_with_locale(GConfEngine* conf, const gchar* key, const gchar* locale, GConfError** err);

gboolean     gconf_set            (GConfEngine* conf, const gchar* key,
                                   GConfValue* value, GConfError** err);

gboolean     gconf_unset          (GConfEngine* conf, const gchar* key, GConfError** err);

/*
 * schema_key should have a schema (if key stores a value) or a dir full of schemas
 * (if key stores a directory name)
 */
gboolean     gconf_associate_schema  (GConfEngine* conf, const gchar* key,
                                      const gchar* schema_key, GConfError** err);

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


gdouble      gconf_get_float (GConfEngine* conf, const gchar* key,
                              GConfError** err);

gint         gconf_get_int   (GConfEngine* conf, const gchar* key,
                              GConfError** err);

/* free the retval, retval can be NULL for "unset" */
gchar*       gconf_get_string(GConfEngine* conf, const gchar* key,
                              GConfError** err);

gboolean     gconf_get_bool  (GConfEngine* conf, const gchar* key,
                              GConfError** err);

/* this one has no default since it would be expensive and make little
   sense; it returns NULL as a default, to indicate unset or error */
/* free the retval */
/* Note that this returns the schema stored at key, NOT
   the schema associated with the key. */
GConfSchema* gconf_get_schema  (GConfEngine* conf, const gchar* key, GConfError** err);

/*
   This automatically converts the list to the given list type;
   a list of int or bool stores values in the list->data field
   using GPOINTER_TO_INT(), a list of strings stores the gchar*
   in list->data, a list of float contains pointers to allocated
   gdouble (gotta love C!).
*/
GSList*      gconf_get_list    (GConfEngine* conf, const gchar* key,
                                GConfValueType list_type, GConfError** err);
/*
  The car_retloc and cdr_retloc args should be the address of the appropriate
  type:
  bool    gboolean*
  int     gint*
  string  gchar**
  float   gdouble*
  schema  GConfSchema**
 */
gboolean     gconf_get_pair    (GConfEngine* conf, const gchar* key,
                                GConfValueType car_type, GConfValueType cdr_type,
                                gpointer car_retloc, gpointer cdr_retloc,
                                GConfError** err);

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

/* List should be the same as the one gconf_get_list() would return */
gboolean     gconf_set_list    (GConfEngine* conf, const gchar* key,
                                GConfValueType list_type,
                                GSList* list,
                                GConfError** err);

gboolean     gconf_set_pair    (GConfEngine* conf, const gchar* key,
                                GConfValueType car_type, GConfValueType cdr_type,
                                gconstpointer address_of_car,
                                gconstpointer address_of_cdr,
                                GConfError** err);


gboolean     gconf_init        (int argc, char **argv, GConfError** err);

/* For use by the Gnome module system */
void gconf_preinit(gpointer app, gpointer mod_info);
void gconf_postinit(gpointer app, gpointer mod_info);

extern const char gconf_version[];

#ifdef HAVE_POPT_H
#include <popt.h>
#endif

#ifdef POPT_AUTOHELP
/* If people are using popt, then make the table available to them */
extern struct poptOption gconf_options[];
#endif

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif



