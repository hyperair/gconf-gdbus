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

#ifndef GCONF_GCONF_CHANGESET_H
#define GCONF_GCONF_CHANGESET_H

#include <gconf.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/*
 * A GConfChangeSet is basically a hash from keys to "changes in value,"
 * where a change in a value is either a new value or "unset this value."
 *
 * You can use this to collect changes then "commit" them as a group to
 * the GConf database.
 */

typedef struct _GConfChangeSet GConfChangeSet;

typedef void (* GConfChangeSetForeachFunc) (GConfChangeSet* cs,
                                            const gchar* key,
                                            GConfValue* value,
                                            gpointer user_data);

gboolean        gconf_commit_change_set   (GConfEngine* conf,
                                           GConfChangeSet* cs,
                                           /* remove all successfully committed
                                              changes from the set */
                                           gboolean remove_commited,
                                           GConfError** err);

GConfChangeSet* gconf_change_set_new      (void);
void            gconf_change_set_ref      (GConfChangeSet* cs);

void            gconf_change_set_unref    (GConfChangeSet* cs);

void            gconf_change_set_clear    (GConfChangeSet* cs);

void            gconf_change_set_remove   (GConfChangeSet* cs,
                                           const gchar* key);

void            gconf_change_set_foreach  (GConfChangeSet* cs,
                                           GConfChangeSetForeachFunc func,
                                           gpointer user_data);

void         gconf_change_set_set         (GConfChangeSet* cs, const gchar* key,
                                           GConfValue* value);

void         gconf_change_set_set_nocopy  (GConfChangeSet* cs, const gchar* key,
                                           GConfValue* value);

void         gconf_change_set_unset      (GConfChangeSet* cs, const gchar* key);

void         gconf_change_set_set_float   (GConfChangeSet* cs, const gchar* key,
                                           gdouble val);

void         gconf_change_set_set_int     (GConfChangeSet* cs, const gchar* key,
                                           gint val);

void         gconf_change_set_set_string  (GConfChangeSet* cs, const gchar* key,
                                           const gchar* val);

void         gconf_change_set_set_bool    (GConfChangeSet* cs, const gchar* key,
                                           gboolean val);

void         gconf_change_set_set_schema  (GConfChangeSet* cs, const gchar* key,
                                           GConfSchema* val);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif



