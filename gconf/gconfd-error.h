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

#ifndef GCONF_GCONFD_ERROR_H
#define GCONF_GCONFD_ERROR_H

#include <glib.h>
#include "gconf-error.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* This global error handling is used by gconfd */

const gchar* gconf_error          (void);
GConfErrNo   gconf_errno          (void);
void         gconf_set_error      (GConfErrNo en, const gchar* format, ...) G_GNUC_PRINTF (2, 3);
void         gconf_clear_error    (void); /* like setting errno to 0 */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif



