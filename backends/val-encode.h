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

#ifndef GCONF_VAL_ENCODE_H

/* Functions to size, serialize and de-serialize a GConfValue, which
 * is stored in the database as an encoded string.
 */

/* All values are stored in the database as strings; bdb_serialize_value()
 * encodes a GConfValue as a string, bdb_restore_value() decodes a
 * serialized string back to a value
 */
extern char *bdb_serialize_value (GConfValue * val, size_t * lenp);

extern GConfValue *bdb_restore_value (const char *srz);

extern void _gconf_check_free (char *buf);

#endif /* GCONF_VAL_ENCODE_H */
