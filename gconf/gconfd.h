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

#ifndef GCONF_GCONFD_H
#define GCONF_GCONFD_H

#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "gconf-error.h"
#include "GConf.h"
#include "gconf-database.h"

PortableServer_POA gconf_get_poa ();

/* return TRUE if the exception was set, clear err if needed */
gboolean gconf_set_exception (GError** err, CORBA_Environment* ev);

gboolean gconfd_logfile_change_listener (GConfDatabase *db,
                                         gboolean add,
                                         guint connection_id,
                                         ConfigListener listener,
                                         const gchar *where,
                                         GError **err);

gboolean gconfd_check_in_shutdown (CORBA_Environment *ev);

void gconfd_need_log_cleanup (void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif



