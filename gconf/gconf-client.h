/* -*- tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
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

#ifndef GCONF_GCONF_GTK_H
#define GCONF_GCONF_GTK_H

#include <gtk/gtkobject.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/*
 * This is a wrapper for the client-side GConf API which provides several
 * convenient features.
 *
 *  - It (recursively) caches the contents of certain directories on
 *    the client side, such as your application's configuration
 *    directory
 * 
 *  - It allows you to register per-key callbacks within these directories,
 *    without having to register multiple server-side callbacks
 *    (g_conf_notify_add() adds a request-for-notify to the server,
 *    this wrapper adds a notify to the server for the whole directory
 *    and keeps your per-key notify requests on the client side).
 *
 *  - It has some error-handling features
 *
 * This class is heavily specialized for per-user desktop applications -
 * even more so than GConf itself.
 */

#define GCONF_TYPE_CLIENT                  (gconf_client_get_type ())
#define GCONF_CLIENT(obj)                  (CONF_CHECK_CAST ((obj), GCONF_TYPE_CLIENT, GConfClient))
#define GCONF_CLIENT_CLASS(klass)          (CONF_CHECK_CLASS_CAST ((klass), GCONF_TYPE_CLIENT, GConfClientClass))
#define GCONF_IS_CLIENT(obj)               (CONF_CHECK_TYPE ((obj), GCONF_TYPE_CLIENT))
#define GCONF_IS_CLIENT_CLASS(klass)       (CONF_CHECK_CLASS_TYPE ((klass), GCONF_TYPE_CLIENT))


typedef struct _GConfClient       GConfClient;
typedef struct _GConfClientClass  GConfClientClass;

struct _GConfClient
{
  GtkObject object;

  /*< private >*/

  GConfEngine* engine;
  
};

struct _GConfClientClass
{
  GtkObjectClass parent_class;

  void (* value_changed) (GConfClient* client,
                          const gchar* relative_key,
                          GConfValue* value);

  /* General note about error handling: AVOID DIALOG DELUGES.
     That is, if lots of errors could happen in a row you need
     to collect those and put them in _one_ dialog, maybe using
     an idle function. gconf_client_basic_error_handler()
     is provided and it does this using GnomeDialog.
  */
  
  /* emitted when you pass NULL for the error return location to a
     GConfClient function and an error occurs. This allows you to
     ignore errors when your generic handler will work, and handle
     them specifically when you need to */
  void (* unreturned_error) (GConfClient* client,
                             GConfError* error);

  /* emitted unconditionally anytime there's an error, whether you ask
     for that error or not. Useful for creating an error log or
     something. */
  void (* error)            (GConfClient* client,
                             GConfError* error);
};


GtkType           gconf_client_get_type        (void);
GConfClient*      gconf_client_new             (const gchar* dirname);
GConfClient*      gconf_client_new_with_engine (const gchar* dirname,
                                                GConfEngine* engine);

/* keys passed to GConfClient instances are relative to the dirname
 * of the GConfClient
 */

void              gconf_client_set             (GConfClient* client,
                                                const gchar* key,
                                                GConfValue* val,
                                                GConfError** err);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif



