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

#ifndef GCONF_GCONF_DATABASE_H
#define GCONF_GCONF_DATABASE_H

#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "gconf-error.h"
#include "GConf.h"
#include "gconf-listeners.h"
#include "gconf-sources.h"
#include "gconf-internals.h"
#include "gconf-glib-private.h"

typedef struct _GConfDatabase GConfDatabase;

struct _GConfDatabase
{
  /* "inherit" from the servant,
     must be first in struct */
  POA_ConfigDatabase servant;

  ConfigDatabase objref;
  
  GConfListeners* listeners;
  GConfSources* sources;

  GTime last_access;
  guint sync_idle;
  guint sync_timeout;
};

GConfDatabase* gconf_database_new     (GConfSources  *sources);
void           gconf_database_destroy (GConfDatabase *db);

CORBA_unsigned_long gconf_database_add_listener     (GConfDatabase       *db,
                                                     ConfigListener       who,
                                                     const gchar         *where);
void                gconf_database_remove_listener  (GConfDatabase       *db,
                                                     CORBA_unsigned_long  cnxn);
void                gconf_database_notify_listeners (GConfDatabase       *db,
                                                     const gchar         *key,
                                                     const ConfigValue   *value,
                                                     gboolean             is_default);


GConfValue* gconf_database_query_value         (GConfDatabase  *db,
                                                const gchar    *key,
                                                const gchar   **locales,
                                                gboolean        use_schema_default,
                                                gboolean       *value_is_default,
                                                GError    **err);
GConfValue* gconf_database_query_default_value (GConfDatabase  *db,
                                                const gchar    *key,
                                                const gchar   **locales,
                                                GError    **err);



void gconf_database_set   (GConfDatabase      *db,
                           const gchar        *key,
                           GConfValue         *value,
                           const ConfigValue  *cvalue,
                           GError        **err);
void gconf_database_unset (GConfDatabase      *db,
                           const gchar        *key,
                           const gchar        *locale,
                           GError        **err);



gboolean gconf_database_dir_exists  (GConfDatabase  *db,
                                     const gchar    *dir,
                                     GError    **err);
void     gconf_database_remove_dir  (GConfDatabase  *db,
                                     const gchar    *dir,
                                     GError    **err);
GSList*  gconf_database_all_entries (GConfDatabase  *db,
                                     const gchar    *dir,
                                     const gchar   **locales,
                                     GError    **err);
GSList*  gconf_database_all_dirs    (GConfDatabase  *db,
                                     const gchar    *dir,
                                     GError    **err);
void     gconf_database_set_schema  (GConfDatabase  *db,
                                     const gchar    *key,
                                     const gchar    *schema_key,
                                     GError    **err);


void     gconf_database_sync             (GConfDatabase  *db,
                                          GError    **err);
gboolean gconf_database_synchronous_sync (GConfDatabase  *db,
                                          GError    **err);
void     gconf_database_clear_cache      (GConfDatabase  *db,
                                          GError    **err);


void gconfd_locale_cache_expire (void);
void gconfd_locale_cache_drop  (void);

GMarkupNode* gconf_database_to_node (GConfDatabase *db, gboolean is_default);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif



