
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
          retval->address = g_strdup(address);
          
          /* Leave a ref on the backend, now held by the GConfSource */
          
          return retval;
        }
    }
}

GConfValue*   
g_conf_source_query_value      (GConfSource* source,
                                const gchar* key,
                                gchar** schema_name)
{
  if (!g_conf_valid_key(key))
    {
      g_conf_set_error(G_CONF_BAD_KEY, _("`%s'"), key);
      return NULL;
    }
  return (*source->backend->vtable->query_value)(source, key, schema_name);
}

void          
g_conf_source_set_value        (GConfSource* source,
                                const gchar* key,
                                GConfValue* value)
{
  g_return_if_fail(value != NULL);
  g_return_if_fail(key != NULL);

  if (!g_conf_valid_key(key))
    {
      g_conf_set_error(G_CONF_BAD_KEY, _("`%s'"), key);
      return;
    }

  g_assert(*key != '\0');

  if (key[1] == '\0')
    {
      g_conf_set_error(G_CONF_IS_DIR, _("The '/' name can only be a directory, not a key"));
      return;
    }

  (*source->backend->vtable->set_value)(source, key, value);
}

void          
g_conf_source_unset_value      (GConfSource* source,
                                const gchar* key)
{
  if (!g_conf_valid_key(key))
    {
      g_conf_set_error(G_CONF_BAD_KEY, _("`%s'"), key);
      return;
    }
  (*source->backend->vtable->unset_value)(source, key);
}

GSList*      
g_conf_source_all_entries         (GConfSource* source,
                                 const gchar* dir)
{
  if (!g_conf_valid_key(dir)) /* keys and directories have the same validity rules */
    {
      g_conf_set_error(G_CONF_BAD_KEY, _("`%s'"), dir);
      return NULL;
    }
  return (*source->backend->vtable->all_entries)(source, dir);
}

GSList*      
g_conf_source_all_dirs          (GConfSource* source,
                                 const gchar* dir)
{
  if (!g_conf_valid_key(dir)) /* keys and directories have the same validity rules */
    {
      g_conf_set_error(G_CONF_BAD_KEY, _("`%s'"), dir);
      return NULL;
    }
  return (*source->backend->vtable->all_subdirs)(source, dir);
}

gboolean
g_conf_source_dir_exists        (GConfSource* source,
                                 const gchar* dir)
{
  if (!g_conf_valid_key(dir)) /* keys and directories have the same validity rules */
    {
      g_conf_set_error(G_CONF_BAD_KEY, _("`%s'"), dir);
      return FALSE;
    }
  
  return (*source->backend->vtable->dir_exists)(source, dir);
}

void         
g_conf_source_remove_dir        (GConfSource* source,
                                 const gchar* dir)
{
  if (!g_conf_valid_key(dir)) /* keys and directories have the same validity rules */
    {
      g_conf_set_error(G_CONF_BAD_KEY, _("`%s'"), dir);
      return;
    }
  return (*source->backend->vtable->remove_dir)(source, dir);
}

void         
g_conf_source_set_schema        (GConfSource* source,
                                 const gchar* key,
                                 const gchar* schema_key)
{
  if (!g_conf_valid_key(key))
    {
      g_conf_set_error(G_CONF_BAD_KEY, _("`%s'"), key);
      return;
    }
  if (!g_conf_valid_key(schema_key))
    {
      g_conf_set_error(G_CONF_BAD_KEY, _("`%s'"), key);
      return;
    }
  
  return (*source->backend->vtable->set_schema)(source, key, schema_key);
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
      g_conf_log(GCL_ERR, _("No '/' in key `%s'"), key);
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

  return retval;
}

const gchar*
g_conf_key_key        (const gchar* key)
{
  const gchar* end;
  
  end = strrchr(key, '/');

  ++end;

  return end;
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
      g_conf_set_error(G_CONF_FAILED, _("info file open failed: %s"), strerror(errno));
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
          g_conf_set_error(G_CONF_FAILED, _("IOR read failed: %s"), strerror(errno));
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
    case SchemaVal:
      type = G_CONF_VALUE_SCHEMA;
      break;
    case ListVal:
      type = G_CONF_VALUE_LIST;
      break;
    case PairVal:
      type = G_CONF_VALUE_PAIR;
      break;
    default:
      g_conf_log(GCL_DEBUG, "Invalid type in %s", __FUNCTION__);
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
    case G_CONF_VALUE_SCHEMA:
      g_conf_value_set_schema_nocopy(gval, 
                                     g_conf_schema_from_corba_schema(&(value->_u.schema_value)));
      break;
    case G_CONF_VALUE_LIST:
      {
        GSList* list = NULL;
        guint i = 0;
        while (i < value->_u.list_value._length)
          {
            GConfValue* val;

            /* This is a bit dubious; we cast a ConfigBasicValue to ConfigValue
               because they have the same initial members, but by the time
               the CORBA and C specs kick in, not sure we are guaranteed
               to be able to do this.
            */
            val = g_conf_value_from_corba_value((ConfigValue*)&value->_u.list_value._buffer[i]);

            list = g_slist_prepend(list, val);

            ++i;
          }
        
        list = g_slist_reverse(list);

        g_conf_value_set_list_nocopy(gval, list);
      }
      break;
    case G_CONF_VALUE_PAIR:
      {
        g_return_val_if_fail(value->_u.pair_value._length == 2, gval);

        g_conf_value_set_car_nocopy(gval,
                                    g_conf_value_from_corba_value((ConfigValue*)&value->_u.list_value._buffer[0]));

        g_conf_value_set_cdr_nocopy(gval,
                                    g_conf_value_from_corba_value((ConfigValue*)&value->_u.list_value._buffer[1]));
      }
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
    case G_CONF_VALUE_SCHEMA:
      cv->_d = SchemaVal;
      fill_corba_schema_from_g_conf_schema(g_conf_value_schema(value),
                                           &cv->_u.schema_value);
      break;
    case G_CONF_VALUE_LIST:
      {
        guint n, i;
        GSList* list;
        
        cv->_d = ListVal;

        list = g_conf_value_list(value);

        n = g_slist_length(list);

        cv->_u.list_value._buffer =
          CORBA_sequence_ConfigBasicValue_allocbuf(n);
        cv->_u.list_value._length = n;
        cv->_u.list_value._maximum = n;

        i= 0;
        while (list != NULL)
          {
            /* That dubious ConfigBasicValue->ConfigValue cast again */
            fill_corba_value_from_g_conf_value((GConfValue*)list->data,
                                               (ConfigValue*)&cv->_u.list_value._buffer[i]);

            list = g_slist_next(list);
            ++i;
          }
      }
      break;
    case G_CONF_VALUE_PAIR:
      {
        cv->_d = PairVal;

        cv->_u.pair_value._buffer =
          CORBA_sequence_ConfigBasicValue_allocbuf(2);
        cv->_u.pair_value._length = 2;
        cv->_u.pair_value._maximum = 2;

        /* dubious cast */
        fill_corba_value_from_g_conf_value(g_conf_value_car(value),
                                           (ConfigValue*)&cv->_u.pair_value._buffer[0]);
        fill_corba_value_from_g_conf_value(g_conf_value_cdr(value),
                                           (ConfigValue*)&cv->_u.pair_value._buffer[1]);
      }
      break;
      
    case G_CONF_VALUE_INVALID:
      cv->_d = InvalidVal;
      break;
    default:
      cv->_d = InvalidVal;
      g_conf_log(GCL_DEBUG, "Unknown type in %s", __FUNCTION__);
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

void          
fill_corba_schema_from_g_conf_schema(GConfSchema* sc, 
                                     ConfigSchema* cs)
{
  switch (sc->type)
    {
    case G_CONF_VALUE_INT:
      cs->value_type = IntVal;
      break;
    case G_CONF_VALUE_BOOL:
      cs->value_type = BoolVal;
      break;
    case G_CONF_VALUE_FLOAT:
      cs->value_type = FloatVal;
      break;
    case G_CONF_VALUE_INVALID:
      cs->value_type = InvalidVal;
      break;
    case G_CONF_VALUE_STRING:
      cs->value_type = StringVal;
      break;
    case G_CONF_VALUE_SCHEMA:
      cs->value_type = SchemaVal;
      break;
    case G_CONF_VALUE_LIST:
      cs->value_type = ListVal;
      break;
    case G_CONF_VALUE_PAIR:
      cs->value_type = PairVal;
      break;
    default:
      g_assert_not_reached();
      break;
    }
  
  cs->short_desc = CORBA_string_dup(sc->short_desc);
  cs->long_desc = CORBA_string_dup(sc->long_desc);
  cs->owner = CORBA_string_dup(sc->owner);
}

ConfigSchema* 
corba_schema_from_g_conf_schema(GConfSchema* sc)
{
  ConfigSchema* cs;

  cs = ConfigSchema__alloc();

  fill_corba_schema_from_g_conf_schema(sc, cs);

  return cs;
}

GConfSchema*  
g_conf_schema_from_corba_schema(const ConfigSchema* cs)
{
  GConfSchema* sc;
  GConfValueType type = G_CONF_VALUE_INVALID;

  switch (cs->value_type)
    {
    case InvalidVal:
      break;
    case StringVal:
      type = G_CONF_VALUE_STRING;
      break;
    case IntVal:
      type = G_CONF_VALUE_INT;
      break;
    case FloatVal:
      type = G_CONF_VALUE_FLOAT;
      break;
    case SchemaVal:
      type = G_CONF_VALUE_SCHEMA;
      break;
    case BoolVal:
      type = G_CONF_VALUE_BOOL;
      break;
    case ListVal:
      type = G_CONF_VALUE_LIST;
      break;
    case PairVal:
      type = G_CONF_VALUE_PAIR;
      break;
    default:
      g_assert_not_reached();
      break;
    }

  sc = g_conf_schema_new();

  g_conf_schema_set_type(sc, type);

  g_conf_schema_set_short_desc(sc, cs->short_desc);
  g_conf_schema_set_long_desc(sc, cs->long_desc);
  g_conf_schema_set_owner(sc, cs->owner);

  return sc;
}

const gchar* 
g_conf_value_type_to_string(GConfValueType type)
{
  switch (type)
    {
    case G_CONF_VALUE_INT:
      return "int";
      break;
    case G_CONF_VALUE_STRING:
      return "string";
      break;
    case G_CONF_VALUE_FLOAT:
      return "float";
      break;
    case G_CONF_VALUE_BOOL:
      return "bool";
      break;
    case G_CONF_VALUE_SCHEMA:
      return "schema";
      break;
    case G_CONF_VALUE_LIST:
      return "list";
      break;
    case G_CONF_VALUE_PAIR:
      return "pair";
      break;
    case G_CONF_VALUE_IGNORE_SUBSEQUENT:
      return "ignore_subseq";
      break;
    default:
      g_assert_not_reached();
      return NULL; /* for warnings */
      break;
    }
}

GConfValueType 
g_conf_value_type_from_string(const gchar* type_str)
{
  if (strcmp(type_str, "int") == 0)
    return G_CONF_VALUE_INT;
  else if (strcmp(type_str, "float") == 0)
    return G_CONF_VALUE_FLOAT;
  else if (strcmp(type_str, "string") == 0)
    return G_CONF_VALUE_STRING;
  else if (strcmp(type_str, "bool") == 0)
    return G_CONF_VALUE_BOOL;
  else if (strcmp(type_str, "schema") == 0)
    return G_CONF_VALUE_SCHEMA;
  else if (strcmp(type_str, "list") == 0)
    return G_CONF_VALUE_LIST;
  else if (strcmp(type_str, "pair") == 0)
    return G_CONF_VALUE_PAIR;
  else if (strcmp(type_str, "ignore_subseq") == 0)
    return G_CONF_VALUE_IGNORE_SUBSEQUENT;
  else
    return G_CONF_VALUE_INVALID;
}

/*
 *   gconfsources
 */

GConfSources* 
g_conf_sources_new(gchar** addresses)
{
  GConfSources* sources;
  GSList* failed = NULL;

  sources = g_new0(GConfSources, 1);

  while (*addresses != NULL)
    {
      GConfSource* source;

      source = g_conf_resolve_address(*addresses);

      if (source != NULL)
        sources->sources = g_list_prepend(sources->sources, source);
      else
        failed = g_slist_prepend(failed, *addresses);

      ++addresses;
    }

  sources->sources = g_list_reverse(sources->sources);

  if (failed != NULL)
    {
      GSList* tmp;
      gchar* all = g_strdup("");

      tmp = failed;

      while (tmp != NULL)
        {
          gchar* old = all;

          all = g_strconcat(old, ", ", tmp->data, NULL);

          g_free(old);

          tmp = g_slist_next(tmp);
        }
      
      g_conf_set_error(G_CONF_BAD_ADDRESS, 
                       _("The following config source addresses were not resolved:\n%s"),
                       all);
      g_free(all);
    }

  return sources;
}

void
g_conf_sources_destroy(GConfSources* sources)
{
  GList* tmp;

  tmp = sources->sources;

  while (tmp != NULL)
    {
      g_conf_source_destroy(tmp->data);
      
      tmp = g_list_next(tmp);
    }

  g_list_free(sources->sources);

  g_free(sources);
}

GConfValue*   
g_conf_sources_query_value (GConfSources* sources, 
                            const gchar* key)
{
  GList* tmp;
  gchar* schema_name = NULL;

  tmp = sources->sources;

  while (tmp != NULL)
    {
      GConfValue* val;

      g_conf_clear_error();

      val = g_conf_source_query_value(tmp->data, key,
                                      schema_name ? NULL : &schema_name); /* once we have one, no more. */

      if (val == NULL)
        {
          switch (g_conf_errno())
            {
            case G_CONF_BAD_KEY:
              /* this isn't getting any better, so bail */
              return NULL;
              break;
            case G_CONF_SUCCESS:
              break;
            default:
              /* weird error, try some other sources */
              break;
            }
        }
      else if (val->type == G_CONF_VALUE_IGNORE_SUBSEQUENT)
        {
          /* Bail now, instead of looking for the standard values */
          g_conf_value_destroy(val);
          break;
        }
      else
        return val;

      tmp = g_list_next(tmp);
    }

  /* If we got here, there was no value; we try to look up the
     schema for this key if we have one, and use the default
     value.
  */

  if (schema_name != NULL)
    {
      GConfValue* val =
        g_conf_sources_query_value(sources, schema_name);
      
      if (val != NULL &&
          val->type != G_CONF_VALUE_SCHEMA)
        {
          g_conf_set_error(G_CONF_FAILED, _("Schema `%s' specified for `%s' stores a non-schema value"), schema_name, key);
                
          g_free(schema_name);

          return NULL;
        }
      else if (val != NULL)
        {
          GConfValue* retval = g_conf_value_schema(val)->default_value;
          /* cheat, "unparent" the value to avoid a copy */
          g_conf_value_schema(val)->default_value = NULL;
          g_conf_value_destroy(val);

          g_free(schema_name);      
          
          return retval;
        }
      else
        {
          g_free(schema_name);
          
          return NULL;
        }
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

  g_conf_clear_error();
  
  while (tmp != NULL)
    {
      GConfSource* src = tmp->data;

      if (src->flags & G_CONF_SOURCE_WRITEABLE)
        {
          /* may set error, we just leave its setting */
          g_conf_source_set_value(src, key, value);
          return;
        }
      else
        {
          /* check whether the value is set; if it is, then
             we return an error since setting an overridden value
             would have no effect
          */
          GConfValue* val;

          val = g_conf_source_query_value(tmp->data, key, NULL);
          
          if (val != NULL)
            {
              g_conf_value_destroy(val);
              g_conf_set_error(G_CONF_OVERRIDDEN,
                               _("Value for `%s' set in a read-only source at the front of your configuration path."), key);
              return;
            }
        } 

      tmp = g_list_next(tmp);
    }
}

void
g_conf_sources_unset_value   (GConfSources* sources,
                              const gchar* key)
{
  /* We unset in every layer we can write to... */
  GList* tmp;

  tmp = sources->sources;

  while (tmp != NULL)
    {
      GConfSource* src = tmp->data;

      if (src->flags & G_CONF_SOURCE_WRITEABLE)
        g_conf_source_unset_value(src, key);    /* we might pile up errors here */
      
      tmp = g_list_next(tmp);
    }
}

gboolean
g_conf_sources_dir_exists (GConfSources* sources,
                           const gchar* dir)
{
  GList *tmp;
  
  tmp = sources->sources;
  
  while (tmp != NULL) 
    {
      GConfSource* src = tmp->data;
      
      if (g_conf_source_dir_exists (src, dir)) 
        return TRUE;

      tmp = g_list_next(tmp);
    }
  
  return FALSE;
}
          
void          
g_conf_sources_remove_dir (GConfSources* sources,
                           const gchar* dir)
{
  /* We remove in every layer we can write to... */
  GList* tmp;

  tmp = sources->sources;

  while (tmp != NULL)
    {
      GConfSource* src = tmp->data;

      if (src->flags & G_CONF_SOURCE_WRITEABLE)
        g_conf_source_remove_dir(src, dir);    /* might pile up errors */
      
      tmp = g_list_next(tmp);
    }
}

void         
g_conf_sources_set_schema        (GConfSources* sources,
                                  const gchar* key,
                                  const gchar* schema_key)
{
  GList* tmp;

  tmp = sources->sources;

  while (tmp != NULL)
    {
      GConfSource* src = tmp->data;

      if (src->flags & G_CONF_SOURCE_WRITEABLE)
        {
          /* may set error, we just leave its setting */
          g_conf_source_set_schema(src, key, schema_key);
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
g_conf_sources_all_entries   (GConfSources* sources,
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
      return g_conf_source_all_entries(sources->sources->data, dir);
    }

  g_assert(g_list_length(sources->sources) > 1);

  hash = g_hash_table_new(g_str_hash, g_str_equal);

  tmp = sources->sources;

  while (tmp != NULL)
    {
      GConfSource* src = tmp->data;
      GSList* pairs = g_conf_source_all_entries(src, dir);
      GSList* iter = pairs;

      while (iter != NULL)
        {
          GConfEntry* pair = iter->data;
          GConfEntry* previous;
          
          if (first_pass)
            previous = NULL; /* Can't possibly be there. */
          else
            previous = g_hash_table_lookup(hash, pair->key);
          
          if (previous != NULL)
            {
              /* Discard */
              g_conf_entry_destroy(pair);
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

GSList*       
g_conf_sources_all_dirs   (GConfSources* sources,
                           const gchar* dir)
{
  GList* tmp = NULL;
  GHashTable* hash = NULL;
  GSList* flattened = NULL;
  gboolean first_pass = TRUE; /* as an optimization, don't bother
                                 doing hash lookups on first source
                              */

  g_return_val_if_fail(sources != NULL, NULL);
  g_return_val_if_fail(dir != NULL, NULL);

  /* As another optimization, skip the whole 
     hash thing if there's only zero or one sources
  */
  if (sources->sources == NULL)
    return NULL;

  if (sources->sources->next == NULL)
    {
      return g_conf_source_all_dirs(sources->sources->data, dir);
    }

  g_assert(g_list_length(sources->sources) > 1);

  hash = g_hash_table_new(g_str_hash, g_str_equal);

  tmp = sources->sources;

  while (tmp != NULL)
    {
      GConfSource* src = tmp->data;
      GSList* subdirs = g_conf_source_all_dirs(src, dir);
      GSList* iter = subdirs;

      while (iter != NULL)
        {
          gchar* subdir = iter->data;
          gchar* previous;
          
          if (first_pass)
            previous = NULL; /* Can't possibly be there. */
          else
            previous = g_hash_table_lookup(hash, subdir);
          
          if (previous != NULL)
            {
              /* Discard */
              g_free(subdir);
            }
          else
            {
              /* Save */
              g_hash_table_insert(hash, subdir, subdir);
            }

          iter = g_slist_next(iter);
        }
      
      /* All pairs are either stored or destroyed. */
      g_slist_free(subdirs);

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

static gchar*
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

static const gchar*
get_variable(const gchar* varname)
{
  /* These first two DO NOT use environment variables, which
     makes things a bit more "secure" in some sense
  */
  if (strcmp(varname, "HOME") == 0)
    {
      return g_get_home_dir();
    }
  else if (strcmp(varname, "USER") == 0)
    {
      return g_get_user_name();
    }
  else if (varname[0] == 'E' &&
           varname[1] == 'N' &&
           varname[2] == 'V' &&
           varname[3] == '_')
    {
      /* This is magic: if a variable called ENV_FOO is used,
         then the environment variable FOO is checked */
      gchar* envvar = getenv(&varname[4]);

      if (envvar)
        return envvar;
      else
        return "";
    }
  else
    return "";
}

static gchar*
subst_variables(const gchar* src)
{
  const gchar* iter;
  gchar* retval;
  guint retval_len;
  guint pos;
  
  g_return_val_if_fail(src != NULL, NULL);

  retval_len = strlen(src) + 1;
  pos = 0;
  
  retval = g_malloc0(retval_len+3); /* add 3 just to avoid off-by-one
                                       segvs - yeah I know it bugs
                                       you, but C sucks */
  
  iter = src;

  while (*iter)
    {
      gboolean performed_subst = FALSE;
      
      if (pos >= retval_len)
        {
          retval_len *= 2;
          retval = g_realloc(retval, retval_len+3); /* add 3 for luck */
        }
      
      if (*iter == '$' && *(iter+1) == '(')
        {
          const gchar* varstart = iter + 2;
          const gchar* varend   = strchr(varstart, ')');

          if (varend != NULL)
            {
              char* varname;
              const gchar* varval;
              guint varval_len;

              performed_subst = TRUE;

              varname = g_strndup(varstart, varend - varstart);
              
              varval = get_variable(varname);
              g_free(varname);

              varval_len = strlen(varval);

              if ((retval_len - pos) < varval_len)
                {
                  retval_len *= 2;
                  retval = g_realloc(retval, retval_len+3);
                }
              
              strcpy(&retval[pos], varval);
              pos += varval_len;

              iter = varend + 1;
            }
        }

      if (!performed_subst)
        {
          retval[pos] = *iter;
          ++pos;
          ++iter;
        }
    }
  retval[pos] = '\0';

  return retval;
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
      g_conf_set_error(G_CONF_FAILED,
                       _("Couldn't open path file `%s': %s\n"), 
                         filename, 
                         strerror(errno));
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
          gchar* varsubst;
          
          unq = unquote_string(buf);
          varsubst = subst_variables(unq);
          
          if (*varsubst != '\0') /* Drop lines with just two quote marks or something */
            {
              g_conf_log(GCL_INFO, _("Adding source `%s'\n"), varsubst);
              l = g_slist_prepend(l, g_strdup(varsubst));
            }
          g_free(varsubst);
        }
    }

  if (ferror(f))
    {
      /* This should basically never happen */
      g_conf_set_error(G_CONF_FAILED,
                       _("Read error on file `%s': %s\n"), 
                       filename, 
                       strerror(errno));
      /* don't return, we want to go ahead and return any 
         addresses we already loaded. */
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

/* This should also support concatting filesystem dirs and keys, 
   or dir and subdir.
*/
gchar*        
g_conf_concat_key_and_dir(const gchar* dir, const gchar* key)
{
  guint dirlen;
  guint keylen;
  gchar* retval;

  g_return_val_if_fail(dir != NULL, NULL);
  g_return_val_if_fail(key != NULL, NULL);
  g_return_val_if_fail(*dir == '/', NULL);

  dirlen = strlen(dir);
  keylen = strlen(key);

  retval = g_malloc0(dirlen+keylen+3); /* auto-null-terminate */

  strcpy(retval, dir);

  if (dir[dirlen-1] == '/')
    {
      /* dir ends in slash, strip key slash if needed */
      if (*key == '/')
        ++key;

      strcpy((retval+dirlen), key);
    }
  else 
    {
      /* Dir doesn't end in slash, add slash if key lacks one. */
      gchar* dest = retval + dirlen;

      if (*key != '/')
        {
          *dest = '/';
          ++dest;
        }
      
      strcpy(dest, key);
    }
  
  return retval;
}

gulong
g_conf_string_to_gulong(const gchar* str)
{
  gulong retval;
  errno = 0;
  retval = strtoul(str, NULL, 10);
  if (errno != 0)
    retval = 0;

  return retval;
}

/*
 * Log
 */

#include <syslog.h>

void
g_conf_log(GConfLogPriority pri, const gchar* fmt, ...)
{
  gchar* msg;
  va_list args;
  int syslog_pri = LOG_DEBUG;
  
  va_start (args, fmt);
  msg = g_strdup_vprintf(fmt, args);
  va_end (args);
  
  switch (pri)
    {
    case GCL_EMERG:
      syslog_pri = LOG_EMERG;
      break;
      
    case GCL_ALERT:
      syslog_pri = LOG_ALERT;
      break;
      
    case GCL_CRIT:
      syslog_pri = LOG_CRIT;
      break;
      
    case GCL_ERR:
      syslog_pri = LOG_ERR;
      break;
      
    case GCL_WARNING:
      syslog_pri = LOG_WARNING;
      break;
      
    case GCL_NOTICE:
      syslog_pri = LOG_NOTICE;
      break;
      
    case GCL_INFO:
      syslog_pri = LOG_INFO;
      break;
      
    case GCL_DEBUG:
      syslog_pri = LOG_DEBUG;
      break;

    default:
      g_assert_not_reached();
      break;
    }

  syslog(syslog_pri, msg);
}
