
/* GConf
 * Copyright (C) 1999 Red Hat Inc.
 * Developed by Havoc Pennington
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


#ifndef GCONF_SCM_GCONF_H
#define GCONF_SCM_GCONF_H

#include <stdlib.h>
#include <libguile.h>
#include <guile/gh.h>
#include "gconf.h"

void g_conf_init_scm(void);

gboolean scm_gconfp (SCM obj);
GConf*   scm2gconf  (SCM obj);
SCM      gconf2scm  (GConf* conf);

#endif
