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

#ifndef GCONF_GCONF_ERROR_H
#define GCONF_GCONF_ERROR_H

#include <glib.h>
#include "gconf-conf.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Error Numbers */

typedef enum {
  G_CONF_SUCCESS = 0,
  G_CONF_FAILED = 1,        /* Something didn't work, don't know why, probably unrecoverable
                               so there's no point having a more specific errno */

  G_CONF_NO_SERVER = 2,     /* Server can't be launched/contacted */
  G_CONF_NO_PERMISSION = 3, /* don't have permission for that */
  G_CONF_BAD_ADDRESS = 4,   /* Address couldn't be resolved */
  G_CONF_BAD_KEY = 5,       /* directory or key isn't valid (contains bad
                               characters, or malformed slash arrangement) */
  G_CONF_PARSE_ERROR = 6,   /* Syntax error when parsing */
  G_CONF_CORRUPT = 7,       /* Fatal error parsing/loading information inside the backend */
  G_CONF_TYPE_MISMATCH = 8, /* Type requested doesn't match type found */
  G_CONF_IS_DIR = 9,        /* Requested key operation on a dir */
  G_CONF_IS_KEY = 10,       /* Requested dir operation on a key */
  G_CONF_OVERRIDDEN = 11    /* Read-only source at front of path has set the value */
} GConfErrNo;

/* Error Object */

typedef struct _GConfError GConfError;

struct _GConfError {
  const gchar* str; /* combination of strerror of the num and additional
                       details; a complete error message. */
  GConfErrNo num;
};

GConfError*  g_conf_error_new      (GConfErrNo en, const gchar* format, ...) G_GNUC_PRINTF (2, 3);
void         g_conf_error_destroy  (GConfError* err);

const gchar* g_conf_error          (void);
GConfErrNo   g_conf_errno          (void);
void         g_conf_set_error      (GConfErrNo en, const gchar* format, ...) G_GNUC_PRINTF (2, 3);
void         g_conf_clear_error    (void); /* like setting errno to 0 */


/* strerror() really shouldn't be used, because g_conf_error() gives
 * a more complete error message.
 */
const gchar* g_conf_strerror       (GConfErrNo en);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif



