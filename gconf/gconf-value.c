
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

#include "gconf-value.h"
#include "gconf-error.h"
#include "gconf-schema.h"
#include "gconf-internals.h"
#include <errno.h>
#include <string.h>
#include <stdlib.h>

static void
set_string(gchar** dest, const gchar* src)
{
  if (*dest != NULL)
    g_free(*dest);

  *dest = src ? g_strdup(src) : NULL;
}

/*
 * Values
 */

GConfValue* 
gconf_value_new(GConfValueType type)
{
  GConfValue* value;

  g_return_val_if_fail(GCONF_VALUE_TYPE_VALID(type), NULL);
  
  /* Probably want to use mem chunks here eventually. */
  value = g_new0(GConfValue, 1);

  value->type = type;

  /* the g_new0() is important: sets list type to invalid, NULLs all
     pointers */
  
  return value;
}

GConfValue* 
gconf_value_new_from_string(GConfValueType type, const gchar* value_str,
                             GError** err)
{
  GConfValue* value;

  value = gconf_value_new(type);

  switch (type)
    {
    case GCONF_VALUE_INT:
      {
        char* endptr = NULL;
        glong result;

        errno = 0;
        result = strtol(value_str, &endptr, 10);

        if (endptr == value_str)
          {
            if (err)
              *err = gconf_error_new(GCONF_ERROR_PARSE_ERROR,
                                      _("Didn't understand `%s' (expected integer)"),
                                      value_str);
            
            gconf_value_free(value);
            value = NULL;
          }
        else if (errno == ERANGE)
          {
            if (err)
              *err = gconf_error_new(GCONF_ERROR_PARSE_ERROR,
                                      _("Integer `%s' is too large or small"),
                                      value_str);
            gconf_value_free(value);
            value = NULL;
          }
        else
          gconf_value_set_int(value, result);
      }
      break;
    case GCONF_VALUE_FLOAT:
      {
        double num;

        if (gconf_string_to_double(value_str, &num))
          {
            gconf_value_set_float(value, num);
          }
        else
          {
            if (err)
              *err = gconf_error_new(GCONF_ERROR_PARSE_ERROR,
                                      _("Didn't understand `%s' (expected real number)"),
                                     value_str);
            
            gconf_value_free(value);
            value = NULL;
          }
      }
      break;
    case GCONF_VALUE_STRING:
      gconf_value_set_string(value, value_str);
      break;
    case GCONF_VALUE_BOOL:
      switch (*value_str)
        {
        case 't':
        case 'T':
        case '1':
        case 'y':
        case 'Y':
          gconf_value_set_bool(value, TRUE);
          break;

        case 'f':
        case 'F':
        case '0':
        case 'n':
        case 'N':
          gconf_value_set_bool(value, FALSE);
          break;
          
        default:
          if (err)
            *err = gconf_error_new(GCONF_ERROR_PARSE_ERROR,
                                   _("Didn't understand `%s' (expected true or false)"),
                                   value_str);
          
          gconf_value_free(value);
          value = NULL;
          break;
        }
      break;
    case GCONF_VALUE_LIST:
    case GCONF_VALUE_PAIR:
    default:
      g_assert_not_reached();
      break;
    }

  return value;
}

static char *
escape_string(const char *str, const char *escaped_chars)
{
  gint i, j, len;
  gchar* ret;

  len = 0;
  for (i = 0; str[i] != '\0'; i++)
    {
      if (strchr(escaped_chars, str[i]) != NULL ||
	  str[i] == '\\')
	len++;
      len++;
    }
  
  ret = g_malloc(len + 1);

  j = 0;
  for (i = 0; str[i] != '\0'; i++)
    {
      if (strchr(escaped_chars, str[i]) != NULL ||
	  str[i] == '\\')
	{
	  ret[j++] = '\\';
	}
      ret[j++] = str[i];
    }
  ret[j++] = '\0';

  return ret;
}

GConfValue*
gconf_value_new_list_from_string(GConfValueType list_type,
                                  const gchar* str,
				  GError** err)
{
  int i, len;
  gboolean escaped, pending_chars;
  GString *string;
  GConfValue* value;
  GSList *list;

  g_return_val_if_fail(list_type != GCONF_VALUE_LIST, NULL);
  g_return_val_if_fail(list_type != GCONF_VALUE_PAIR, NULL);

  if (str[0] != '[')
    {
      if (err)
	*err = gconf_error_new(GCONF_ERROR_PARSE_ERROR,
			       _("Didn't understand `%s' (list must start with a '[')"),
			       str);
      return NULL;
    }

  len = strlen(str);

  /* Note: by now len is sure to be 1 or larger, so len-1 will never be
   * negative */
  if (str[len-1] != ']')
    {
      if (err)
	*err = gconf_error_new(GCONF_ERROR_PARSE_ERROR,
			       _("Didn't understand `%s' (list must end with a ']')"),
			       str);
      return NULL;
    }

  if (strstr(str, "[]"))
    {
      value = gconf_value_new(GCONF_VALUE_LIST);
      gconf_value_set_list_type(value, list_type);

      return value;
    }

  escaped = FALSE;
  pending_chars = FALSE;
  list = NULL;
  string = g_string_new(NULL);

  for (i = 1; str[i] != '\0'; i++)
    {
      if ( ! escaped &&
	  (str[i] == ',' ||
	   str[i] == ']'))
	{
	  GConfValue* val;
	  val = gconf_value_new_from_string(list_type, string->str, err);

	  if (err && *err != NULL)
	    {
	      /* Free values so far */
	      g_slist_foreach(list, (GFunc)gconf_value_free, NULL);
	      g_slist_free(list);

	      g_string_free(string, TRUE);

	      return NULL;
	    }

	  g_string_assign(string, "");
	  list = g_slist_prepend(list, val);
	  if (str[i] == ']' &&
	      i != len-1)
	    {
	      /* Free values so far */
	      g_slist_foreach(list, (GFunc)gconf_value_free, NULL);
	      g_slist_free(list);

	      g_string_free(string, TRUE);

	      if (err)
		*err = gconf_error_new(GCONF_ERROR_PARSE_ERROR,
				       _("Didn't understand `%s' (extra unescaped ']' found inside list)"),
				       str);
	      return NULL;
	    }
	  pending_chars = FALSE;
	}
      else if ( ! escaped && str[i] == '\\')
	{
	  escaped = TRUE;
	  pending_chars = TRUE;
	}
      else
	{
	  g_string_append_c(string, str[i]);
	  escaped = FALSE;
	  pending_chars = TRUE;
	}
    }

  g_string_free(string, TRUE);

  if (pending_chars)
    {
      /* Free values so far */
      g_slist_foreach(list, (GFunc)gconf_value_free, NULL);
      g_slist_free(list);

      g_string_free(string, TRUE);

      if (err)
	*err = gconf_error_new(GCONF_ERROR_PARSE_ERROR,
			       _("Didn't understand `%s' (extra trailing characters)"),
			       str);
      return NULL;
    }

  /* inverse list as we were prepending to it */
  list = g_slist_reverse(list);

  value = gconf_value_new(GCONF_VALUE_LIST);
  gconf_value_set_list_type(value, list_type);

  gconf_value_set_list_nocopy(value, list);

  return value;
}

GConfValue*
gconf_value_new_pair_from_string(GConfValueType car_type,
                                  GConfValueType cdr_type,
                                  const gchar* str,
				  GError** err)
{
  int i, len;
  int elem;
  gboolean escaped, pending_chars;
  GString *string;
  GConfValue* value;
  GConfValue* car;
  GConfValue* cdr;

  g_return_val_if_fail(car_type != GCONF_VALUE_LIST, NULL);
  g_return_val_if_fail(car_type != GCONF_VALUE_PAIR, NULL);
  g_return_val_if_fail(cdr_type != GCONF_VALUE_LIST, NULL);
  g_return_val_if_fail(cdr_type != GCONF_VALUE_PAIR, NULL);

  if (str[0] != '(')
    {
      if (err)
	*err = gconf_error_new(GCONF_ERROR_PARSE_ERROR,
			       _("Didn't understand `%s' (pair must start with a '(')"),
			       str);
      return NULL;
    }

  len = strlen(str);

  /* Note: by now len is sure to be 1 or larger, so len-1 will never be
   * negative */
  if (str[len-1] != ')')
    {
      if (err)
	*err = gconf_error_new(GCONF_ERROR_PARSE_ERROR,
			       _("Didn't understand `%s' (pair must end with a ')')"),
			       str);
      return NULL;
    }

  escaped = FALSE;
  pending_chars = FALSE;
  car = cdr = NULL;
  string = g_string_new(NULL);
  elem = 0;

  for (i = 1; str[i] != '\0'; i++)
    {
      if ( ! escaped &&
	  (str[i] == ',' ||
	   str[i] == ')'))
	{
	  if ((str[i] == ')' && elem != 1) ||
	      (elem > 1))
	    {
	      /* Free values so far */
	      if (car)
	        gconf_value_free(car);
	      if (cdr)
	        gconf_value_free(cdr);

	      g_string_free(string, TRUE);

	      if (err)
		*err = gconf_error_new(GCONF_ERROR_PARSE_ERROR,
				       _("Didn't understand `%s' (wrong number of elements)"),
				       str);
	      return NULL;
	    }

	  if (elem == 0)
	    car = gconf_value_new_from_string(car_type, string->str, err);
	  else if (elem == 1)
	    cdr = gconf_value_new_from_string(cdr_type, string->str, err);

	  elem ++;

	  if (err && *err != NULL)
	    {
	      /* Free values so far */
	      if (car)
	        gconf_value_free(car);
	      if (cdr)
	        gconf_value_free(cdr);

	      g_string_free(string, TRUE);

	      return NULL;
	    }

	  g_string_assign(string, "");

	  if (str[i] == ')' &&
	      i != len-1)
	    {
	      /* Free values so far */
	      if (car)
	        gconf_value_free(car);
	      if (cdr)
	        gconf_value_free(cdr);

	      g_string_free(string, TRUE);

	      if (err)
		*err = gconf_error_new(GCONF_ERROR_PARSE_ERROR,
				       _("Didn't understand `%s' (extra unescaped ')' found inside pair)"),
				       str);
	      return NULL;
	    }
	  pending_chars = FALSE;
	}
      else if ( ! escaped && str[i] == '\\')
	{
	  escaped = TRUE;
	  pending_chars = TRUE;
	}
      else
	{
	  g_string_append_c(string, str[i]);
	  escaped = FALSE;
	  pending_chars = TRUE;
	}
    }

  g_string_free(string, TRUE);

  if (pending_chars)
    {
      /* Free values so far */
      if (car)
	gconf_value_free(car);
      if (cdr)
	gconf_value_free(cdr);

      if (err)
	*err = gconf_error_new(GCONF_ERROR_PARSE_ERROR,
			       _("Didn't understand `%s' (extra trailing characters)"),
			       str);
      return NULL;
    }

  if (elem != 2)
    {
      /* Free values so far */
      if (car)
	gconf_value_free(car);
      if (cdr)
	gconf_value_free(cdr);

      if (err)
	*err = gconf_error_new(GCONF_ERROR_PARSE_ERROR,
			       _("Didn't understand `%s' (wrong number of elements)"),
			       str);
      return NULL;
    }

  value = gconf_value_new(GCONF_VALUE_PAIR);
  gconf_value_set_car_nocopy(value, car);
  gconf_value_set_cdr_nocopy(value, cdr);

  return value;
}

gchar*
gconf_value_to_string(GConfValue* value)
{
  /* These strings shouldn't be translated; they're primarily 
     intended for machines to read, not humans, though I do
     use them in some debug spew
  */
  gchar* retval = NULL;

  switch (value->type)
    {
    case GCONF_VALUE_INT:
      retval = g_strdup_printf("%d", gconf_value_get_int(value));
      break;
    case GCONF_VALUE_FLOAT:
      retval = gconf_double_to_string(gconf_value_get_float(value));
      break;
    case GCONF_VALUE_STRING:
      retval = g_strdup(gconf_value_get_string(value));
      break;
    case GCONF_VALUE_BOOL:
      retval = gconf_value_get_bool(value) ? g_strdup("true") : g_strdup("false");
      break;
    case GCONF_VALUE_LIST:
      {
        GSList* list;

        list = gconf_value_get_list(value);

        if (list == NULL)
          retval = g_strdup("[]");
        else
          {
            gchar* buf = NULL;
            guint bufsize = 64;
            guint cur = 0;

            g_assert(list != NULL);
            
            buf = g_malloc(bufsize+3); /* my +3 superstition */
            
            buf[0] = '[';
            ++cur;

            g_assert(cur < bufsize);
            
            while (list != NULL)
              {
                gchar* tmp;
                gchar* elem;
                guint len;
                
                tmp = gconf_value_to_string((GConfValue*)list->data);

                g_assert(tmp != NULL);

		elem = escape_string(tmp, ",]");

		g_free(tmp);

                len = strlen(elem);

                if ((cur + len + 2) >= bufsize) /* +2 for '\0' and comma */
                  {
                    bufsize = MAX(bufsize*2, bufsize+len+4); 
                    buf = g_realloc(buf, bufsize+3);
                  }

                g_assert(cur < bufsize);
                
                strcpy(&buf[cur], elem);
                cur += len;

                g_assert(cur < bufsize);
                
                g_free(elem);

                buf[cur] = ',';
                ++cur;

                g_assert(cur < bufsize);
                
                list = g_slist_next(list);
              }

            g_assert(cur < bufsize);
            
            buf[cur-1] = ']'; /* overwrites last comma */
            buf[cur] = '\0';

            retval = buf;
          }
      }
      break;
    case GCONF_VALUE_PAIR:
      {
        gchar* tmp;
        gchar* car;
        gchar* cdr;

        tmp = gconf_value_to_string(gconf_value_get_car(value));
	car = escape_string(tmp, ",)");
	g_free(tmp);
        tmp = gconf_value_to_string(gconf_value_get_cdr(value));
	cdr = escape_string(tmp, ",)");
	g_free(tmp);
        retval = g_strdup_printf("(%s,%s)", car, cdr);
        g_free(car);
        g_free(cdr);
      }
      break;
      /* These remaining shouldn't really be used outside of debug spew... */
    case GCONF_VALUE_INVALID:
      retval = g_strdup("Invalid");
      break;
    case GCONF_VALUE_SCHEMA:
      {
        const gchar* locale;
        const gchar* type;
        const gchar* list_type;
        const gchar* car_type;
        const gchar* cdr_type;
        
        locale = gconf_schema_get_locale(gconf_value_get_schema(value));
        type = gconf_value_type_to_string(gconf_schema_get_type(gconf_value_get_schema(value)));
        list_type = gconf_value_type_to_string(gconf_schema_get_list_type(gconf_value_get_schema(value)));
        car_type = gconf_value_type_to_string(gconf_schema_get_car_type(gconf_value_get_schema(value)));
        cdr_type = gconf_value_type_to_string(gconf_schema_get_cdr_type(gconf_value_get_schema(value)));
        
        retval = g_strdup_printf("Schema (type: `%s' list_type: '%s' "
				 "car_type: '%s' cdr_type: '%s' locale: `%s')",
                                 type, list_type, car_type, cdr_type,
				 locale ? locale : "(null)");
      }
      break;
    default:
      g_assert_not_reached();
      break;
    }

  return retval;
}

static GSList*
copy_value_list(GSList* list)
{
  GSList* copy = NULL;
  GSList* tmp = list;
  
  while (tmp != NULL)
    {
      copy = g_slist_prepend(copy, gconf_value_copy(tmp->data));
      
      tmp = g_slist_next(tmp);
    }
  
  copy = g_slist_reverse(copy);

  return copy;
}

GConfValue* 
gconf_value_copy(GConfValue* src)
{
  GConfValue* dest;

  g_return_val_if_fail(src != NULL, NULL);

  dest = gconf_value_new(src->type);

  switch (src->type)
    {
    case GCONF_VALUE_INT:
    case GCONF_VALUE_FLOAT:
    case GCONF_VALUE_BOOL:
    case GCONF_VALUE_INVALID:
      dest->d = src->d;
      break;
    case GCONF_VALUE_STRING:
      set_string(&dest->d.string_data, src->d.string_data);
      break;
    case GCONF_VALUE_SCHEMA:
      if (src->d.schema_data)
        dest->d.schema_data = gconf_schema_copy(src->d.schema_data);
      else
        dest->d.schema_data = NULL;
      break;
      
    case GCONF_VALUE_LIST:
      {
        GSList* copy;

        copy = copy_value_list(src->d.list_data.list);

        dest->d.list_data.list = copy;
        dest->d.list_data.type = src->d.list_data.type;
      }
      break;
      
    case GCONF_VALUE_PAIR:

      if (src->d.pair_data.car)
        dest->d.pair_data.car = gconf_value_copy(src->d.pair_data.car);
      else
        dest->d.pair_data.car = NULL;

      if (src->d.pair_data.cdr)
        dest->d.pair_data.cdr = gconf_value_copy(src->d.pair_data.cdr);
      else
        dest->d.pair_data.cdr = NULL;

      break;
      
    default:
      g_assert_not_reached();
    }
  
  return dest;
}

static void
gconf_value_free_list(GConfValue* value)
{
  GSList* tmp;
  g_return_if_fail(value != NULL);
  g_return_if_fail(value->type == GCONF_VALUE_LIST);
  
  tmp = value->d.list_data.list;

  while (tmp != NULL)
    {
      gconf_value_free(tmp->data);
      
      tmp = g_slist_next(tmp);
    }
  g_slist_free(value->d.list_data.list);

  value->d.list_data.list = NULL;
}

void 
gconf_value_free(GConfValue* value)
{
  g_return_if_fail(value != NULL);

  switch (value->type)
    {
    case GCONF_VALUE_STRING:
      if (value->d.string_data != NULL)
        g_free(value->d.string_data);
      break;
    case GCONF_VALUE_SCHEMA:
      if (value->d.schema_data != NULL)
        gconf_schema_free(value->d.schema_data);
      break;
    case GCONF_VALUE_LIST:
      gconf_value_free_list(value);
      break;
    case GCONF_VALUE_PAIR:
      if (value->d.pair_data.car != NULL)
        gconf_value_free(value->d.pair_data.car);

      if (value->d.pair_data.cdr != NULL)
        gconf_value_free(value->d.pair_data.cdr);
      break;
    default:
      break;
    }
  
  g_free(value);
}

void        
gconf_value_set_int(GConfValue* value, gint the_int)
{
  g_return_if_fail(value != NULL);
  g_return_if_fail(value->type == GCONF_VALUE_INT);

  value->d.int_data = the_int;
}

void        
gconf_value_set_string(GConfValue* value, const gchar* the_str)
{
  g_return_if_fail(value != NULL);
  g_return_if_fail(value->type == GCONF_VALUE_STRING);

  set_string(&value->d.string_data, the_str);
}

void        
gconf_value_set_float(GConfValue* value, gdouble the_float)
{
  g_return_if_fail(value != NULL);
  g_return_if_fail(value->type == GCONF_VALUE_FLOAT);

  value->d.float_data = the_float;
}

void        
gconf_value_set_bool(GConfValue* value, gboolean the_bool)
{
  g_return_if_fail(value != NULL);
  g_return_if_fail(value->type == GCONF_VALUE_BOOL);

  value->d.bool_data = the_bool;
}

void       
gconf_value_set_schema(GConfValue* value, GConfSchema* sc)
{
  g_return_if_fail(value != NULL);
  g_return_if_fail(value->type == GCONF_VALUE_SCHEMA);
  
  if (value->d.schema_data != NULL)
    gconf_schema_free(value->d.schema_data);

  value->d.schema_data = gconf_schema_copy(sc);
}

void        
gconf_value_set_schema_nocopy(GConfValue* value, GConfSchema* sc)
{
  g_return_if_fail(value != NULL);
  g_return_if_fail(value->type == GCONF_VALUE_SCHEMA);
  g_return_if_fail(sc != NULL);
  
  if (value->d.schema_data != NULL)
    gconf_schema_free(value->d.schema_data);

  value->d.schema_data = sc;
}

void
gconf_value_set_car(GConfValue* value, GConfValue* car)
{
  g_return_if_fail(car != NULL);

  gconf_value_set_car_nocopy(value, gconf_value_copy(car));
}

void
gconf_value_set_car_nocopy(GConfValue* value, GConfValue* car)
{
  g_return_if_fail(value != NULL);
  g_return_if_fail(value->type == GCONF_VALUE_PAIR);
  g_return_if_fail(car != NULL);
  
  if (value->d.pair_data.car != NULL)
    gconf_value_free(value->d.pair_data.car);

  value->d.pair_data.car = car;
}

void
gconf_value_set_cdr(GConfValue* value, GConfValue* cdr)
{
  g_return_if_fail(cdr != NULL);

  gconf_value_set_cdr_nocopy(value, gconf_value_copy(cdr));
}

void
gconf_value_set_cdr_nocopy(GConfValue* value, GConfValue* cdr)
{
  g_return_if_fail(value != NULL);
  g_return_if_fail(value->type == GCONF_VALUE_PAIR);
  g_return_if_fail(cdr != NULL);
  
  if (value->d.pair_data.cdr != NULL)
    gconf_value_free(value->d.pair_data.cdr);

  value->d.pair_data.cdr = cdr;
}

void
gconf_value_set_list_type(GConfValue* value,
                           GConfValueType type)
{
  g_return_if_fail(value != NULL);
  g_return_if_fail(value->type == GCONF_VALUE_LIST);
  g_return_if_fail(type != GCONF_VALUE_LIST);
  g_return_if_fail(type != GCONF_VALUE_PAIR);
  /* If the list is non-NULL either we already have the right
     type, or we shouldn't be changing it without deleting
     the list first. */
  g_return_if_fail(value->d.list_data.list == NULL);

  value->d.list_data.type = type;
}

void
gconf_value_set_list_nocopy(GConfValue* value,
                             GSList* list)
{
  g_return_if_fail(value != NULL);
  g_return_if_fail(value->type == GCONF_VALUE_LIST);
  g_return_if_fail(value->d.list_data.type != GCONF_VALUE_INVALID);
  
  if (value->d.list_data.list)
    gconf_value_free_list(value);

  value->d.list_data.list = list;
}

void
gconf_value_set_list       (GConfValue* value,
                             GSList* list)
{
  g_return_if_fail(value != NULL);
  g_return_if_fail(value->type == GCONF_VALUE_LIST);
  g_return_if_fail(value->d.list_data.type != GCONF_VALUE_INVALID);
  g_return_if_fail((list == NULL) ||
                   ((list->data != NULL) &&
                    (((GConfValue*)list->data)->type == value->d.list_data.type)));
  
  if (value->d.list_data.list)
    gconf_value_free_list(value);

  value->d.list_data.list = copy_value_list(list);
}

/*
 * GConfMetaInfo
 */

GConfMetaInfo*
gconf_meta_info_new(void)
{
  GConfMetaInfo* gcmi;

  gcmi = g_new0(GConfMetaInfo, 1);

  /* pointers and time are NULL/0 */
  
  return gcmi;
}

void
gconf_meta_info_free(GConfMetaInfo* gcmi)
{
  g_free(gcmi);
}

void
gconf_meta_info_set_schema  (GConfMetaInfo* gcmi,
                              const gchar* schema_name)
{
  set_string(&gcmi->schema, schema_name);
}

void
gconf_meta_info_set_mod_user(GConfMetaInfo* gcmi,
                              const gchar* mod_user)
{
  set_string(&gcmi->mod_user, mod_user);
}

void
gconf_meta_info_set_mod_time(GConfMetaInfo* gcmi,
                              GTime mod_time)
{
  gcmi->mod_time = mod_time;
}

/*
 * GConfEntry
 */

GConfEntry*
gconf_entry_new (const gchar *key,
                 GConfValue  *val)
{
  return gconf_entry_new_nocopy (g_strdup (key),
                                 val ? gconf_value_copy (val) : NULL);

}

GConfEntry* 
gconf_entry_new_nocopy(gchar* key, GConfValue* val)
{
  GConfEntry* pair;

  pair = g_new(GConfEntry, 1);

  pair->key   = key;
  pair->value = val;
  pair->schema_name = NULL;
  pair->is_default = FALSE;
  
  return pair;
}

void
gconf_entry_free(GConfEntry* pair)
{
  g_free(pair->key);
  if (pair->value)
    gconf_value_free(pair->value);
  g_free(pair);
}

GConfValue*
gconf_entry_steal_value (GConfEntry* entry)
{
  GConfValue* val = entry->value;
  entry->value = NULL;
  return val;
}

void
gconf_entry_set_value (GConfEntry  *entry,
                       GConfValue  *val)
{
  gconf_entry_set_value_nocopy (entry,
                                val ? gconf_value_copy (val) : NULL);
}

void
gconf_entry_set_value_nocopy(GConfEntry* entry,
                             GConfValue* val)
{
  if (entry->value)
    gconf_value_free(entry->value);

  entry->value = val;
}

void
gconf_entry_set_schema_name(GConfEntry* entry,
                            const gchar* name)
{
  if (entry->schema_name)
    g_free(entry->schema_name);

  entry->schema_name = name ? g_strdup(name) : NULL;
}

void
gconf_entry_set_is_default (GConfEntry* entry,
                            gboolean is_default)
{
  entry->is_default = is_default;
}


