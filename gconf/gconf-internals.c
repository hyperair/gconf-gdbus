
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
#include <ctype.h>


/* Quick hack so I can mark strings */

#ifdef _ 
#warning "_ already defined"
#else
#define _(x) x
#endif

#ifdef N_ 
#warning "N_ already defined"
#else
#define N_(x) x
#endif


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
    case G_CONF_VALUE_BOOL:
      if (*value_str == 't' || *value_str == 'T' || *value_str == '1')
        g_conf_value_set_bool(value, TRUE);
      else if (*value_str == 'f' || *value_str == 'F' || *value_str == '0')
        g_conf_value_set_bool(value, FALSE);
      else
        {
          g_warning("Didn't understand `%s' (expected true or false)", value_str);
          g_conf_value_destroy(value);
          value = NULL;
        }
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
    case G_CONF_VALUE_BOOL:
      retval = g_conf_value_bool(value) ? g_strdup("true") : g_strdup("false");
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
    case G_CONF_VALUE_BOOL:
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

void        
g_conf_value_set_bool(GConfValue* value, gboolean the_bool)
{
  g_return_if_fail(value != NULL);
  g_return_if_fail(value->type == G_CONF_VALUE_BOOL);

  value->d.bool_data = the_bool;
}

GConfPair* 
g_conf_pair_new(gchar* key, GConfValue* val)
{
  GConfPair* pair;

  pair = g_new(GConfPair, 1);

  pair->key   = key;
  pair->value = val;

  return pair;
}

void
g_conf_pair_destroy(GConfPair* pair)
{
  g_free(pair->key);
  g_conf_value_destroy(pair->value);
  g_free(pair);
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

GSList*      
g_conf_source_all_pairs         (GConfSource* source,
                                 const gchar* dir)
{
  if (!g_conf_valid_key(dir)) /* keys and directories have the same validity rules */
    {
      g_warning("Invalid directory `%s'", dir);
    }
  return (*source->backend->vtable->all_entries)(source, dir);
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
  gchar* host_name = NULL;

  info_dir = g_conf_server_info_dir();

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
    case InvalidVal:
      return NULL;
      break;
    case IntVal:
      type = G_CONF_VALUE_INT;
      break;
    case StringVal:
      type = G_CONF_VALUE_STRING;
      break;
    case FloatVal:
      type = G_CONF_VALUE_FLOAT;
      break;
    case BoolVal:
      type = G_CONF_VALUE_BOOL;
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
    case G_CONF_VALUE_BOOL:
      g_conf_value_set_bool(gval, value->_u.bool_value);
      break;
    default:
      g_assert_not_reached();
      break;
    }
  
  return gval;
}

void          
fill_corba_value_from_g_conf_value(GConfValue* value, 
                                   ConfigValue* cv)
{
  if (value == NULL)
    {
      cv->_d = InvalidVal;
      return;
    }

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
    case G_CONF_VALUE_BOOL:
      cv->_d = BoolVal;
      cv->_u.bool_value = g_conf_value_bool(value);
      break;
    case G_CONF_VALUE_INVALID:
      cv->_d = InvalidVal;
      break;
    default:
      cv->_d = InvalidVal;
      g_warning("Unknown type in %s", __FUNCTION__);
      break;
    }
}

ConfigValue*  
corba_value_from_g_conf_value(GConfValue* value)
{
  ConfigValue* cv;

  cv = ConfigValue__alloc();

  fill_corba_value_from_g_conf_value(value, cv);

  return cv;
}

ConfigValue*  
invalid_corba_value()
{
  ConfigValue* cv;

  cv = ConfigValue__alloc();

  cv->_d = InvalidVal;

  return cv;
}

/*
 *   GConfSources
 */

GConfSources* 
g_conf_sources_new(gchar** addresses)
{
  GConfSources* sources;

  sources = g_new0(GConfSources, 1);

  while (*addresses != NULL)
    {
      GConfSource* source;

      source = g_conf_resolve_address(*addresses);

      if (source != NULL)
        sources->sources = g_list_prepend(sources->sources, source);
      else
        g_warning("Didn't resolve `%s'", *addresses); /* FIXME, better error reporting */

      ++addresses;
    }

  sources->sources = g_list_reverse(sources->sources);

  return sources;
}

GConfValue*   
g_conf_sources_query_value (GConfSources* sources, 
                            const gchar* key)
{
  GList* tmp;

  tmp = sources->sources;

  while (tmp != NULL)
    {
      GConfValue* val;

      val = g_conf_source_query_value(tmp->data, key);

      if (val != NULL)
        return val;

      tmp = g_list_next(tmp);
    }

  return NULL;
}


void
g_conf_sources_set_value   (GConfSources* sources,
                            const gchar* key,
                            GConfValue* value)
{
  GList* tmp;

  tmp = sources->sources;

  while (tmp != NULL)
    {
      GConfSource* src = tmp->data;

      if (src->flags & G_CONF_SOURCE_WRITEABLE)
        {
          g_conf_source_set_value(src, key, value);
          return;
        }

      tmp = g_list_next(tmp);
    }
}


/* God, this is depressingly inefficient. Maybe there's a nicer way to
   implement it... */
/* Then we have to ship it all to the app via CORBA... */
/* Anyway, we use a hash to be sure we have a single value for 
   each key in the directory, and we always take that value from
   the first source that had one set. When we're done we flatten
   the hash.
*/
static void
hash_listify_func(gpointer key, gpointer value, gpointer user_data)
{
  GSList** list_p = user_data;

  *list_p = g_slist_prepend(*list_p, value);
}

GSList*       
g_conf_sources_all_pairs   (GConfSources* sources,
                            const gchar* dir)
{
  GList* tmp;
  GHashTable* hash;
  GSList* flattened;
  gboolean first_pass = TRUE; /* as an optimization, don't bother
                                 doing hash lookups on first source
                              */

  /* As another optimization, skip the whole 
     hash thing if there's only zero or one sources
  */
  if (sources->sources == NULL)
    return NULL;

  if (sources->sources->next == NULL)
    {
      return g_conf_source_all_pairs(sources->sources->data, dir);
    }

  g_assert(g_list_length(sources->sources) > 1);

  hash = g_hash_table_new(g_str_hash, g_str_equal);

  tmp = sources->sources;

  while (tmp != NULL)
    {
      GConfSource* src = tmp->data;
      GSList* pairs = g_conf_source_all_pairs(src, dir);
      GSList* iter = pairs;

      while (iter != NULL)
        {
          GConfPair* pair = iter->data;
          GConfPair* previous;
          
          if (first_pass)
            previous = NULL; /* Can't possibly be there. */
          else
            previous = g_hash_table_lookup(hash, pair->key);
          
          if (previous != NULL)
            {
              /* Discard */
              g_conf_pair_destroy(pair);
            }
          else
            {
              /* Save */
              g_hash_table_insert(hash, pair->key, pair);
            }

          iter = g_slist_next(iter);
        }
      
      /* All pairs are either stored or destroyed. */
      g_slist_free(pairs);

      first_pass = FALSE;

      tmp = g_list_next(tmp);
    }

  flattened = NULL;

  g_hash_table_foreach(hash, hash_listify_func, &flattened);

  g_hash_table_destroy(hash);

  return flattened;
}

gboolean
g_conf_sources_sync_all    (GConfSources* sources)
{
  GList* tmp;
  gboolean failed = FALSE;

  tmp = sources->sources;

  while (tmp != NULL)
    {
      GConfSource* src = tmp->data;

      if (!g_conf_source_sync_all(src))
        failed = TRUE;

      tmp = g_list_next(tmp);
    }

  return !failed;
}

/*
 * Config files (yikes! we can't store our config in GConf!)
 */

gchar*
unquote_string(gchar* s)
{
  gchar* end;

  /* Strip whitespace and first quote from front of string */
  while (*s && (isspace(*s) || (*s == '"')))
    ++s;

  end = s;
  while (*end)
    ++end;

  --end; /* one back from '\0' */

  /* Strip whitespace and last quote from end of string */
  while ((end > s) && (isspace(*end) || (*end == '"')))
    {
      *end = '\0';
      --end;
    }

  return s;
}

gchar**       
g_conf_load_source_path(const gchar* filename)
{
  FILE* f;
  GSList* l = NULL;
  gchar** addresses;
  gchar buf[512];
  GSList* tmp;
  guint n;

  f = fopen(filename, "r");

  if (f == NULL)
    {
      fprintf(stderr, 
              "Didn't open file `%s': %s\n", filename, strerror(errno));
      /* FIXME better error */
      return NULL;
    }

  while (fgets(buf, 512, f) != NULL)
    {
      gchar* s = buf;
      
      while (*s && isspace(*s))
        ++s;

      if (*s == '#')
        {
          /* Allow comments, why not */
        }
      else if (*s == '\0')
        {
          /* Blank line */
        }
      else if (strncmp("include", s, 7) == 0)
        {
          gchar* unq;
          gchar** included;

          s += 7;

          unq = unquote_string(s);

          included = g_conf_load_source_path(unq);

          if (included != NULL)
            {
              gchar** iter = included;

              printf("Including file `%s'\n", unq);

              while (*iter)
                {
                  l = g_slist_prepend(l, *iter); /* Note that we won't free *included */
                  ++iter;
                }

              g_free(included); /* Only the array, not the contained strings */
            }
        }
      else 
        {
          gchar* unq;

          unq = unquote_string(buf);

          if (*unq != '\0') /* Drop lines with just two quote marks or something */
            {
              printf("Adding source `%s'\n", unq);
              l = g_slist_prepend(l, g_strdup(unq));
            }
        }
    }

  fclose(f);  

  /* This will make sense if you realize that we reversed the list 
     as we loaded it, and are now reversing it to be correct again. 
  */

  if (l == NULL)
    return NULL;

  n = g_slist_length(l);

  g_assert(n > 0);
  
  addresses = g_malloc0(sizeof(gchar*) * (n+1));

  addresses[n] = NULL;

  --n;
  tmp = l;

  while (tmp != NULL)
    {
      addresses[n] = tmp->data;

      tmp = g_slist_next(tmp);
      --n;
    }
  
  g_assert(addresses[0] != NULL); /* since we used malloc0 this detects bad logic */

  return addresses;
}
