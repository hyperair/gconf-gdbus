/*
 * GConf BerkeleyDB back-end
 * 
 * Copyright (C) 2000 Sun Microsystems Inc Contributed to the GConf project.
 * 
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Library General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 * 
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Library General Public
 * License for more details.
 * 
 * You should have received a copy of the GNU Library General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <string.h>

#include <gconf/gconf.h>

/* required for gconf_value_new_from_string() */
#include <gconf/gconf-internals.h>

/* GConf Value type character identifiers */
static const char bdb_string = 's';
static const char bdb_int = 'i';
static const char bdb_float = 'f';
static const char bdb_bool = 'b';
static const char bdb_schema = 'x';
static const char bdb_list = 'l';
static const char bdb_pair = 'p';

/* Map each character value type id to the corresponding GConfValueType
   and vice-versa */
static struct s_TypeMap
{
  char type;
  GConfValueType valuetype;
}
bdb_value_types[] =
{
  {
  's', GCONF_VALUE_STRING}
  ,
  {
  'i', GCONF_VALUE_INT}
  ,
  {
  'f', GCONF_VALUE_FLOAT}
  ,
  {
  'b', GCONF_VALUE_BOOL}
  ,
  {
  'x', GCONF_VALUE_SCHEMA}
  ,
  {
  'l', GCONF_VALUE_LIST}
  ,
  {
  'p', GCONF_VALUE_PAIR}
  ,
  {
  '\0', GCONF_VALUE_INVALID}
};

/* { Conversion functions using bdb_value_types to map between GConfValueType
 * and the corresponding character-based value type identifiers
 */
static GConfValueType
get_value_type (char type)
{
  int i = 0;

  while (bdb_value_types[i].type && (bdb_value_types[i].type != type))
    i++;
  return bdb_value_types[i].valuetype;
}

static char
get_type_for_value_type (GConfValueType valuetype)
{
  int i = 0;

  while (bdb_value_types[i].type
	 && (bdb_value_types[i].valuetype != valuetype))
    i++;
  return bdb_value_types[i].type;
}

/* } */

static char *
append_string (char *buf, const char *string)
{
  if (string)
    {
      strcpy (buf, string);
      return buf + strlen (string) + 1;
    }
  *buf++ = '\0';
  return buf;
}

static char tbuf[259];		/* 256 + 3 bytes for "<type>:" and end zero byte */

void
_gconf_check_free (char *buf)
{
  if (buf && buf != tbuf)
    free (buf);
}

/* { Functions to size, serialize and de-serialize a GConfValue, which
 *   is stored in the database as an encoded string.
 */

/* bdb_size_value returns the encoded size of a value, which is 2 bytes
 * for leading type info, plus enough room for the value as a string
 * (excluding terminal NULL byte)
 */
static size_t
bdb_size_value (const GConfValue * val)
{
  size_t len = 0;
  char *buf = tbuf;
  if (!val)
    return 3;			/* empty values are encoded as a type and a
				 * null string */
  switch (val->type)
    {
    case GCONF_VALUE_STRING:
      {
	char *t = gconf_value_get_string (val) == 0 ? "" : val->d.string_data;
	len = strlen (t) + 2;
      }
      break;
    case GCONF_VALUE_INT:
      sprintf (buf, "%d", val->d.int_data);
      len = strlen (buf) + 2;
      break;
    case GCONF_VALUE_FLOAT:
      sprintf (buf, "%f", (double) gconf_value_get_float (val));
      len = strlen (buf) + 2;
      break;
    case GCONF_VALUE_BOOL:
      len = 3;
      break;
    case GCONF_VALUE_SCHEMA:
      {
	GConfSchema *schema = gconf_value_get_schema (val);
	len = 3;
	if (schema == NULL)
	  {
	    return len;
	  }
	if (schema->locale)
	  len += strlen (schema->locale);	/* Schema locale */
	len++;
	if (schema->owner)
	  len += strlen (schema->owner);	/* Name of creating application */
	len++;
	if (schema->short_desc)
	  len += strlen (schema->short_desc);	/* 40 char or less
						 * description, no newlines */
	len++;
	if (schema->long_desc)
	  len += strlen (schema->long_desc);	/* could be a paragraph or so */
	len++;
	len += bdb_size_value (schema->default_value);	/* includes type
							 * information */
	if (!schema->default_value)
	  {
	    if (schema->type == GCONF_VALUE_LIST)
	      len++;		/* even an empty list will include the type
				 * character */
	  }
      }
      break;
    case GCONF_VALUE_LIST:
      {
	GSList *list;
	len = 4;
	list = gconf_value_get_list (val);
	while (list != NULL)
	  {
	    len += bdb_size_value ((GConfValue *) list->data) + 1;
	    list = g_slist_next (list);
	  }
	return len;
      }
    case GCONF_VALUE_PAIR:
      len = 2 + bdb_size_value (gconf_value_get_car (val)) +
	bdb_size_value (gconf_value_get_cdr (val));
    case GCONF_VALUE_INVALID:
    default:
      len = 0;
      break;
    }
  return len;
}

/* All values are stored in the database as strings; bdb_serialize_value()
 * encodes a GConfValue as a string, bdb_restore_value() decodes a
 * serialized string back to a value
 */
char *
bdb_serialize_value (GConfValue * val, size_t * lenp)
{
  char *buf = tbuf;
  char *t;
  size_t len = 0;
  g_assert (val != 0);
  switch (val->type)
    {
    case GCONF_VALUE_STRING:
      t = gconf_value_get_string (val) == 0 ? "" : val->d.string_data;
      len = strlen (t) + 3;
      if (len > 256)
	{
	  buf = (char *) malloc (len);
	}
      buf[0] = bdb_string;
      buf[1] = ':';
      strcpy (buf + 2, t);
      break;
    case GCONF_VALUE_INT:
      sprintf (buf, "%c:%d", bdb_int, gconf_value_get_int (val));
      len = strlen (buf) + 1;
      break;
    case GCONF_VALUE_FLOAT:
      sprintf (buf, "%c:%f", bdb_float, (double) gconf_value_get_float (val));
      len = strlen (buf) + 1;
      break;
    case GCONF_VALUE_BOOL:
      sprintf (buf, "%c:%d", bdb_bool, gconf_value_get_bool (val) ? 1 : 0);
      len = strlen (buf) + 1;
      break;
    case GCONF_VALUE_SCHEMA:
      {
	GConfSchema *schema = gconf_value_get_schema (val);
	size_t sublen;
	char *end;
	len = bdb_size_value (val);
	buf = (char *) malloc (len);
	buf[0] = bdb_schema;
	buf[1] = ':';
	if (schema == NULL)
	  {
	    buf[2] = '\0';
	    return buf;
	  }
	end = &buf[2];
	end = append_string (end, schema->locale);
	end = append_string (end, schema->owner);
	end = append_string (end, schema->short_desc);
	end = append_string (end, schema->long_desc);
	if (!schema->default_value)
	  {
	    *end++ = get_type_for_value_type (schema->type);
	    *end++ = ':';
	    *end++ = '\0';
	  }
	else
	  {
	    t = bdb_serialize_value (schema->default_value, &sublen);
	    memcpy (end, t, sublen);
	    end += sublen;
	  }
      }
      break;
    case GCONF_VALUE_LIST:
      {
	GSList *list;
	char *end;
	size_t sublen;
	len = bdb_size_value (val);
	buf = (char *) malloc (len);
	list = val->d.list_data.list;
	buf[0] = bdb_list;
	buf[1] = ':';
	buf[2] = get_type_for_value_type (gconf_value_get_list_type (val));
	end = buf + 3;
	while (list != NULL)
	  {
	    t = bdb_serialize_value ((GConfValue *) list->data, &sublen);
	    memcpy (end, t, sublen);
	    end += sublen;
	    _gconf_check_free (t);
	    list = g_slist_next (list);
	  }
	*end = '\0';
      }
      break;
    case GCONF_VALUE_PAIR:
      {
	size_t sublen;
	len = bdb_size_value (val);
	buf = (char *) malloc (len);
	buf[0] = bdb_pair;
	buf[1] = ':';
	len = 2;
	t = bdb_serialize_value (gconf_value_get_car (val), &sublen);
	if (t)
	  {
	    memcpy (buf + len, t, sublen);
	    len += sublen;
	    _gconf_check_free (t);
	  }
	else
	  {
	    buf[len++] = '\0';
	  }
	t = bdb_serialize_value (gconf_value_get_cdr (val), &sublen);
	if (t)
	  {
	    memcpy (buf + len, t, sublen);
	    len += sublen;
	    _gconf_check_free (t);
	  }
	else
	  {
	    buf[len++] = '\0';
	  }
      }
      break;
    case GCONF_VALUE_INVALID:
    default:
      *lenp = 0;
      return NULL;
      break;
    }
  *lenp = len;
  return buf;
}

GConfValue *
bdb_restore_value (const char *srz)
{
  size_t len;
  char type;
  GError *err;
  g_assert (srz != 0);
  if ((strlen (srz) < 2) || (srz[1] != ':'))
    {
      return NULL;
    }
  type = *srz;
  srz += 2;
  switch (type)
    {
    case 's':
      return gconf_value_new_from_string (GCONF_VALUE_STRING, srz, &err);
      break;
    case 'i':
      return gconf_value_new_from_string (GCONF_VALUE_INT, srz, &err);
      break;
    case 'f':
      return gconf_value_new_from_string (GCONF_VALUE_FLOAT, srz, &err);
      break;
    case 'b':
      return gconf_value_new_from_string (GCONF_VALUE_BOOL, srz, &err);
      break;
    case 'x':
      {
	GConfValue *schema_val = gconf_value_new (GCONF_VALUE_SCHEMA);
	GConfValue *val;
	GConfSchema *schema = gconf_schema_new ();
	len = 4;		/* "x:" + (char)type + '\0' */
	if (*srz)
	  gconf_schema_set_locale (schema, srz);
	srz += strlen (srz) + 1;
	if (*srz)
	  gconf_schema_set_owner (schema, srz);
	srz += strlen (srz) + 1;
	if (*srz)
	  gconf_schema_set_short_desc (schema, srz);
	srz += strlen (srz) + 1;
	if (*srz)
	  gconf_schema_set_long_desc (schema, srz);
	srz += strlen (srz) + 1;
	val = bdb_restore_value (srz);
	gconf_schema_set_type (schema, get_value_type (*srz));
	gconf_schema_set_default_value_nocopy (schema, val);
	gconf_value_set_schema (schema_val, schema);
	return schema_val;
      }
      break;
    case 'l':
      {
	GSList *list = NULL;
	GConfValue *valuep;
	valuep = gconf_value_new (GCONF_VALUE_LIST);
	gconf_value_set_list_type (valuep, get_value_type (*srz++));
	while (*srz)
	  {
	    list = g_slist_append (list, bdb_restore_value (srz));
	    while (*srz)
	      srz++;
	    srz++;
	  }
	gconf_value_set_list (valuep, list);
	_gconf_slist_free_all (list);
	return valuep;
      }
      break;
    case 'p':
      {
	GConfValue *valuep = NULL;
	if (*srz)
	  {
	    valuep = gconf_value_new (GCONF_VALUE_PAIR);
	    gconf_value_set_car (valuep, bdb_restore_value (srz));
	    while (*srz)
	      srz++;
	    srz++;
	    if (*srz)
	      {
		gconf_value_set_cdr (valuep, bdb_restore_value (srz));
	      }
	    else
	      {
		gconf_value_free (valuep);
		valuep = NULL;
	      }
	  }
	return valuep;
      }
    default:
      break;
    }
  return 0;
}

/* } */
