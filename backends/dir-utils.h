/* 
 * GConf BerkeleyDB back-end
 *
 * Copyright (C) 2000 Sun Microsystems Inc
 * Contributed to the GConf project.
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

#ifndef GCONF_DIR_UTILS_H

/* g_free()s each element's data, then free's the list */
extern void _gconf_slist_free_all (GSList * list);

/* parses root directory of a file-based GConf database and checks
 * directory existence/writeability/locking
 */
extern char *_gconf_get_root_dir (const char *address, guint * pflags,
				  const gchar * dbtype, GError ** err);

#endif /* GCONF_DIR_UTILS_H */
