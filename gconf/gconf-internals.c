
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

#include "gconf-internals.h"
#include "gconf-backend.h"
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>


/*
 * Values
 */

GConfValue* 
g_conf_value_new(GConfValueType type)
{
  GConfValue* value;

  /* Probably want to use mem chunks here eventually. */
  value = g_new0(GConfValue, 1);

  value->type = type;

  return value;
}

GConfValue* 
g_conf_value_new_from_string(GConfValueType type, const gchar* value_str)
{
  GConfValue* value;

  value = g_conf_value_new(type);

  switch (type)
    {
    case G_CONF_VALUE_INT:
      g_conf_value_set_int(value, atoi(value_str));
      break;
    case G_CONF_VALUE_FLOAT:
      {
        gchar* endptr = 0;
        double num;
        num = g_strtod(value_str, &endptr);
        if (value_str != endptr)
          {
            g_conf_value_set_float(value, num);
          }
        else
          {
            g_warning("Didn't understand `%s' (expected real number)",
                      value_str);
            
            g_conf_value_destroy(value);
            value = NULL;
          }
      }
      break;
    case G_CONF_VALUE_STRING:
      g_conf_value_set_string(value, value_str);
      break;
    default:
      g_assert_not_reached();
      break;
    }

  return value;
}

gchar*
g_conf_value_to_string(GConfValue* value)
{
  gchar* retval = NULL;

  switch (value->type)
    {
    case G_CONF_VALUE_INT:
      retval = g_malloc(64);
      g_snprintf(retval, 64, "%d", g_conf_value_int(value));
      break;
    case G_CONF_VALUE_FLOAT:
      retval = g_malloc(64);
      g_snprintf(retval, 64, "%g", g_conf_value_float(value));
      break;
    case G_CONF_VALUE_STRING:
      retval = g_strdup(g_conf_value_string(value));
      break;
    default:
      g_assert_not_reached();
      break;
    }

  return retval;
}

GConfValue* 
g_conf_value_copy(GConfValue* src)
{
  GConfValue* dest;

  g_return_val_if_fail(src != NULL, NULL);

  dest = g_conf_value_new(src->type);

  switch (src->type)
    {
    case G_CONF_VALUE_INT:
    case G_CONF_VALUE_FLOAT:
      dest->d = src->d;
      break;
    case G_CONF_VALUE_STRING:
      if (src->d.string_data)
        dest->d.string_data = g_strdup(src->d.string_data);
      else 
        dest->d.string_data = NULL;
      break;
    default:
      g_assert_not_reached();
    }
  
  return dest;
}

void 
g_conf_value_destroy(GConfValue* value)
{
  g_return_if_fail(value != NULL);

  if (value->type == G_CONF_VALUE_STRING && 
      value->d.string_data != NULL)
    g_free(value->d.string_data);

  g_free(value);
}

void        
g_conf_value_set_int(GConfValue* value, gint the_int)
{
  g_return_if_fail(value != NULL);
  g_return_if_fail(value->type == G_CONF_VALUE_INT);

  value->d.int_data = the_int;
}

void        
g_conf_value_set_string(GConfValue* value, const gchar* the_str)
{
  g_return_if_fail(value != NULL);
  g_return_if_fail(value->type == G_CONF_VALUE_STRING);

  value->d.string_data = g_strdup(the_str);
}

void        
g_conf_value_set_float(GConfValue* value, gdouble the_float)
{
  g_return_if_fail(value != NULL);
  g_return_if_fail(value->type == G_CONF_VALUE_FLOAT);

  value->d.float_data = the_float;
}


/* 
 *  Sources
 */

GConfSource* 
g_conf_resolve_address(const gchar* address)
{
  GConfBackend* backend;

  backend = g_conf_get_backend(address);

  if (backend == NULL)
    return NULL;
  else
    {
      GConfSource* retval;

      retval = g_conf_backend_resolve_address(backend, address);

      if (retval == NULL)
        {
          g_conf_backend_unref(backend);
          return NULL;
        }
      else
        {
          retval->backend = backend;
          
          /* Leave a ref on the backend, now held by the GConfSource */
          
          return retval;
        }
    }
}

GConfValue*   
g_conf_source_query_value      (GConfSource* source,
                                const gchar* key)
{
  if (!g_conf_valid_key(key))
    {
      g_warning("Invalid key `%s'", key);
      return NULL;
    }
  return (*source->backend->vtable->query_value)(source, key);
}

void          
g_conf_source_set_value        (GConfSource* source,
                                const gchar* key,
                                GConfValue* value)
{
  if (!g_conf_valid_key(key))
    {
      g_warning("Invalid key `%s'", key);
    }
  (*source->backend->vtable->set_value)(source, key, value);
}

gboolean
g_conf_source_sync_all         (GConfSource* source)
{
  return (*source->backend->vtable->sync_all)(source);
}

void         
g_conf_source_destroy (GConfSource* source)
{
  GConfBackend* backend = source->backend;

  (*source->backend->vtable->destroy_source)(source);

  /* Remove ref held by the source. */
  g_conf_backend_unref(backend);
}

/* 
 * Ampersand and <> are not allowed due to the XML backend; shell
 * special characters aren't allowed; others are just in case we need
 * some magic characters someday.  hyphen, underscore, period, colon
 * are allowed as separators.  
 */

static const gchar invalid_chars[] = "\"$&<>,+=#!()'|{}[]?~`;\\";

gboolean     
g_conf_valid_key      (const gchar* key)
{
  const gchar* s = key;
  gboolean just_saw_slash = FALSE;

  /* Key must start with the root */
  if (*key != '/')
    return FALSE;

  while (*s)
    {
      if (just_saw_slash)
        {
          /* Can't have two slashes in a row, since it would mean
           * an empty spot.
           * Can't have a period right after a slash,
           * because it would be a pain for filesystem-based backends.
           */
          if (*s == '/' || *s == '.')
            return FALSE;
        }

      if (*s == '/')
        {
          just_saw_slash = TRUE;
        }
      else
        {
          const gchar* inv = invalid_chars;

          just_saw_slash = FALSE;

          while (*inv)
            {
              if (*inv == *s)
                return FALSE;
              ++inv;
            }
        }

      ++s;
    }

  /* Can't end with slash */
  if (just_saw_slash)
    return FALSE;
  else
    return TRUE;
}

gchar*
g_conf_key_directory  (const gchar* key)
{
  const gchar* end;
  gchar* retval;
  int len;

  end = strrchr(key, '/');

  if (end == NULL)
    {
      g_warning("No '/' in key `%s'", key);
      return NULL;
    }

  len = end-key+1;

  if (len == 1)
    {
      /* Root directory */
      retval = g_strdup("/");
    }
  else 
    {
      retval = g_malloc(len);
      
      strncpy(retval, key, len);
      
      retval[len-1] = '\0';
    }

  printf("dir is `%s'\n", retval);

  return retval;
}

gchar*
g_conf_key_key        (const gchar* key)
{
  const gchar* end;
  
  end = strrchr(key, '/');

  ++end;

  return g_strdup(end);
}

/*
 *  Random stuff 
 */

gboolean
g_conf_file_exists (const gchar* filename)
{
  struct stat s;
  
  g_return_val_if_fail (filename != NULL,FALSE);
  
  return stat (filename, &s) == 0;
}

gboolean
g_conf_file_test(const gchar* filename, int test)
{
  struct stat s;
  if(stat (filename, &s) != 0)
    return FALSE;
  if(!(test & G_CONF_FILE_ISFILE) && S_ISREG(s.st_mode))
    return FALSE;
  if(!(test & G_CONF_FILE_ISLINK) && S_ISLNK(s.st_mode))
    return FALSE;
  if(!(test & G_CONF_FILE_ISDIR) && S_ISDIR(s.st_mode))
    return FALSE;
  return TRUE;
}

gchar*   
g_conf_server_info_file(void)
{
  gchar* info_dir;
  gchar* entire_file;
  gchar buf[256];
  gchar* host_name = NULL;

  info_dir = g_conf_server_info_dir();

  /* Decided different machines should share the same gconfd. */
#if 0
  if (gethostname(buf, 256) < 0)
    {
      g_warning("GConf failed to get host name; may cause trouble if you're using the same home dir on > 1 machines");
      host_name = NULL;
    }
  else
    {
      host_name = buf;
    }
#endif  

  entire_file = g_strconcat(info_dir, "/.gconfd.info", host_name ? "." : NULL, host_name, NULL);

  g_free(info_dir);

  return entire_file;
}

gchar*   
g_conf_server_info_dir(void)
{
  const gchar* home_dir;

  home_dir = g_get_home_dir();

  return g_strconcat(home_dir, "/.gconfd", NULL);
}

gchar* 
g_conf_read_server_ior(void)
{
  int fd;
  gchar* info_file;

  info_file = g_conf_server_info_file();

  /* We could detect this if the open fails, but 
     the file not existing isn't an error, failure 
     to open it is an error, and we want to distinguish the 
     cases.
  */
  if (!g_conf_file_exists(info_file))
    {
      g_free(info_file);
      return NULL;
    }

  fd = open(info_file, O_RDONLY);

  g_free(info_file);

  if (fd < 0)
    {
      g_warning("info file open failed: %s", strerror(errno));
      return NULL;
    }
  else
    {
      gchar buf[512];
      int bytes_read;

      bytes_read = read(fd, buf, 510);

      close(fd);

      if (bytes_read < 0)
        {
          g_warning("IOR read failed: %s", strerror(errno));
          return NULL;
        }
      else
        {
          buf[bytes_read] = '\0';
          return g_strdup(buf);
        }
    }
}

GConfValue* 
g_conf_value_from_corba_value(const ConfigValue* value)
{
  GConfValue* gval;
  GConfValueType type = G_CONF_VALUE_INVALID;
  
  switch (value->_d)
    {
    case IntVal:
      type = G_CONF_VALUE_INT;
      break;
    case StringVal:
      type = G_CONF_VALUE_STRING;
      break;
    case FloatVal:
      type = G_CONF_VALUE_FLOAT;
      break;
    default:
      g_warning("Invalid type in %s", __FUNCTION__);
      return NULL;
    }

  gval = g_conf_value_new(type);

  switch (gval->type)
    {
    case G_CONF_VALUE_INT:
      g_conf_value_set_int(gval, value->_u.int_value);
      break;
    case G_CONF_VALUE_STRING:
      g_conf_value_set_string(gval, value->_u.string_value);
      break;
    case G_CONF_VALUE_FLOAT:
      g_conf_value_set_float(gval, value->_u.float_value);
      break;
    default:
      g_assert_not_reached();
      break;
    }
  
  return gval;
}

ConfigValue*  
corba_value_from_g_conf_value(GConfValue* value)
{
  ConfigValue* cv;

  cv = ConfigValue__alloc();

  switch (value->type)
    {
    case G_CONF_VALUE_INT:
      cv->_d = IntVal;
      cv->_u.int_value = g_conf_value_int(value);
      break;
    case G_CONF_VALUE_STRING:
      cv->_d = StringVal;
      cv->_u.string_value = CORBA_string_dup(g_conf_value_string(value));
      break;
    case G_CONF_VALUE_FLOAT:
      cv->_d = FloatVal;
      cv->_u.float_value = g_conf_value_float(value);
      break;
    default:
      g_warning("Unknown type in %s", __FUNCTION__);
      return NULL;
      break;
    }

  return cv;
}

