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

#ifndef GCONF_GCONF_ORBIT_H
#define GCONF_GCONF_ORBIT_H

#include "GConf.h"
#include <glib.h>
#include "gconf-error.h"

/* Sets up orb to work with a glib main loop, just as libgnorba does. */
CORBA_ORB
gconf_init_orb(int* argc, char** argv, GConfError** err);

/* Assumes orb is already set up (for example, via libgnorba)
   and just tells GConf where to find it 
*/
void 
gconf_set_orb(CORBA_ORB orb);

CORBA_ORB
gconf_get_orb(void);

#endif


