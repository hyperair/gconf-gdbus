
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
#include "gconf-schema.h"
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

gchar*
gconf_key_directory  (const gchar* key)
{
  const gchar* end;
  gchar* retval;
  int len;

  end = strrchr(key, '/');

  if (end == NULL)
    {
      gconf_log(GCL_ERR, _("No '/' in key `%s'"), key);
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
gconf_key_key        (const gchar* key)
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
gconf_file_exists (const gchar* filename)
{
  struct stat s;
  
  g_return_val_if_fail (filename != NULL,FALSE);
  
  return stat (filename, &s) == 0;
}

gboolean
gconf_file_test(const gchar* filename, int test)
{
  struct stat s;
  if(stat (filename, &s) != 0)
    return FALSE;
  if(!(test & GCONF_FILE_ISFILE) && S_ISREG(s.st_mode))
    return FALSE;
  if(!(test & GCONF_FILE_ISLINK) && S_ISLNK(s.st_mode))
    return FALSE;
  if(!(test & GCONF_FILE_ISDIR) && S_ISDIR(s.st_mode))
    return FALSE;
  return TRUE;
}

gchar*   
gconf_server_info_file(void)
{
  gchar* info_dir;
  gchar* entire_file;
  gchar* host_name = NULL;

  info_dir = gconf_server_info_dir();

  entire_file = g_strconcat(info_dir, "/.gconfd.info", host_name ? "." : NULL, host_name, NULL);

  g_free(info_dir);

  return entire_file;
}

gchar*   
gconf_server_info_dir(void)
{
  const gchar* home_dir;

  home_dir = g_get_home_dir();

  return g_strconcat(home_dir, "/.gconfd", NULL);
}

gchar* 
gconf_read_server_ior(GConfError** err)
{
  int fd;
  gchar* info_file;

  info_file = gconf_server_info_file();

  /* We could detect this if the open fails, but 
     the file not existing isn't an error, failure 
     to open it is an error, and we want to distinguish the 
     cases.
  */
  if (!gconf_file_exists(info_file))
    {
      if (err)
        *err = gconf_error_new(GCONF_FAILED, _("Server information file `%s' is missing"), info_file);
      g_free(info_file);
      return NULL;
    }

  fd = open(info_file, O_RDONLY);

  g_free(info_file);

  if (fd < 0)
    {
      if (err)
        *err = gconf_error_new(GCONF_FAILED, _("Info file open failed: %s"), strerror(errno));
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
          if (err)
            *err = gconf_error_new(GCONF_FAILED, _("IOR read failed: %s"), strerror(errno));
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
gconf_value_from_corba_value(const ConfigValue* value)
{
  GConfValue* gval;
  GConfValueType type = GCONF_VALUE_INVALID;
  
  switch (value->_d)
    {
    case InvalidVal:
      return NULL;
      break;
    case IntVal:
      type = GCONF_VALUE_INT;
      break;
    case StringVal:
      type = GCONF_VALUE_STRING;
      break;
    case FloatVal:
      type = GCONF_VALUE_FLOAT;
      break;
    case BoolVal:
      type = GCONF_VALUE_BOOL;
      break;
    case SchemaVal:
      type = GCONF_VALUE_SCHEMA;
      break;
    case ListVal:
      type = GCONF_VALUE_LIST;
      break;
    case PairVal:
      type = GCONF_VALUE_PAIR;
      break;
    default:
      gconf_log(GCL_DEBUG, "Invalid type in %s", __FUNCTION__);
      return NULL;
    }

  gval = gconf_value_new(type);

  switch (gval->type)
    {
    case GCONF_VALUE_INT:
      gconf_value_set_int(gval, value->_u.int_value);
      break;
    case GCONF_VALUE_STRING:
      gconf_value_set_string(gval, value->_u.string_value);
      break;
    case GCONF_VALUE_FLOAT:
      gconf_value_set_float(gval, value->_u.float_value);
      break;
    case GCONF_VALUE_BOOL:
      gconf_value_set_bool(gval, value->_u.bool_value);
      break;
    case GCONF_VALUE_SCHEMA:
      gconf_value_set_schema_nocopy(gval, 
                                     gconf_schema_from_corba_schema(&(value->_u.schema_value)));
      break;
    case GCONF_VALUE_LIST:
      {
        GSList* list = NULL;
        guint i = 0;
        
        switch (value->_u.list_value.list_type)
          {
          case BIntVal:
            gconf_value_set_list_type(gval, GCONF_VALUE_INT);
            break;
          case BBoolVal:
            gconf_value_set_list_type(gval, GCONF_VALUE_BOOL);
            break;
          case BFloatVal:
            gconf_value_set_list_type(gval, GCONF_VALUE_FLOAT);
            break;
          case BStringVal:
            gconf_value_set_list_type(gval, GCONF_VALUE_STRING);
            break;
          case BInvalidVal:
            break;
          default:
            g_warning("Bizarre list type in %s", __FUNCTION__);
            break;
          }

        i = 0;
        while (i < value->_u.list_value.seq._length)
          {
            GConfValue* val;

            /* This is a bit dubious; we cast a ConfigBasicValue to ConfigValue
               because they have the same initial members, but by the time
               the CORBA and C specs kick in, not sure we are guaranteed
               to be able to do this.
            */
            val = gconf_value_from_corba_value((ConfigValue*)&value->_u.list_value.seq._buffer[i]);

            if (val->type != gconf_value_list_type(gval))
              g_warning("Incorrect type for list element in %s", __FUNCTION__);
            else
              list = g_slist_prepend(list, val);

            ++i;
          }
        
        list = g_slist_reverse(list);
            
        gconf_value_set_list_nocopy(gval, list);
      }
      break;
    case GCONF_VALUE_PAIR:
      {
        g_return_val_if_fail(value->_u.pair_value._length == 2, gval);
        
        gconf_value_set_car_nocopy(gval,
                                    gconf_value_from_corba_value((ConfigValue*)&value->_u.list_value.seq._buffer[0]));

        gconf_value_set_cdr_nocopy(gval,
                                    gconf_value_from_corba_value((ConfigValue*)&value->_u.list_value.seq._buffer[1]));
      }
      break;
    default:
      g_assert_not_reached();
      break;
    }
  
  return gval;
}

void          
fill_corba_value_from_gconf_value(GConfValue* value, 
                                   ConfigValue* cv)
{
  if (value == NULL)
    {
      cv->_d = InvalidVal;
      return;
    }

  switch (value->type)
    {
    case GCONF_VALUE_INT:
      cv->_d = IntVal;
      cv->_u.int_value = gconf_value_int(value);
      break;
    case GCONF_VALUE_STRING:
      cv->_d = StringVal;
      cv->_u.string_value = CORBA_string_dup(gconf_value_string(value));
      break;
    case GCONF_VALUE_FLOAT:
      cv->_d = FloatVal;
      cv->_u.float_value = gconf_value_float(value);
      break;
    case GCONF_VALUE_BOOL:
      cv->_d = BoolVal;
      cv->_u.bool_value = gconf_value_bool(value);
      break;
    case GCONF_VALUE_SCHEMA:
      cv->_d = SchemaVal;
      fill_corba_schema_from_gconf_schema(gconf_value_schema(value),
                                           &cv->_u.schema_value);
      break;
    case GCONF_VALUE_LIST:
      {
        guint n, i;
        GSList* list;
        
        cv->_d = ListVal;

        list = gconf_value_list(value);

        n = g_slist_length(list);

        cv->_u.list_value.seq._buffer =
          CORBA_sequence_ConfigBasicValue_allocbuf(n);
        cv->_u.list_value.seq._length = n;
        cv->_u.list_value.seq._maximum = n;
        CORBA_sequence_set_release(&cv->_u.list_value.seq, TRUE);
        
        switch (gconf_value_list_type(value))
          {
          case GCONF_VALUE_INT:
            cv->_u.list_value.list_type = BIntVal;
            break;

          case GCONF_VALUE_BOOL:
            cv->_u.list_value.list_type = BBoolVal;
            break;
            
          case GCONF_VALUE_STRING:
            cv->_u.list_value.list_type = BStringVal;
            break;

          case GCONF_VALUE_FLOAT:
            cv->_u.list_value.list_type = BFloatVal;
            break;

          case GCONF_VALUE_SCHEMA:
            cv->_u.list_value.list_type = BSchemaVal;
            break;
            
          default:
            cv->_u.list_value.list_type = BInvalidVal;
            g_warning("Invalid list type in %s", __FUNCTION__);
            break;
          }
        
        i= 0;
        while (list != NULL)
          {
            /* That dubious ConfigBasicValue->ConfigValue cast again */
            fill_corba_value_from_gconf_value((GConfValue*)list->data,
                                               (ConfigValue*)&cv->_u.list_value.seq._buffer[i]);

            list = g_slist_next(list);
            ++i;
          }
      }
      break;
    case GCONF_VALUE_PAIR:
      {
        cv->_d = PairVal;

        cv->_u.pair_value._buffer =
          CORBA_sequence_ConfigBasicValue_allocbuf(2);
        cv->_u.pair_value._length = 2;
        cv->_u.pair_value._maximum = 2;
        CORBA_sequence_set_release(&cv->_u.pair_value, TRUE);
        
        /* dubious cast */
        fill_corba_value_from_gconf_value(gconf_value_car(value),
                                           (ConfigValue*)&cv->_u.pair_value._buffer[0]);
        fill_corba_value_from_gconf_value(gconf_value_cdr(value),
                                           (ConfigValue*)&cv->_u.pair_value._buffer[1]);
      }
      break;
      
    case GCONF_VALUE_INVALID:
      cv->_d = InvalidVal;
      break;
    default:
      cv->_d = InvalidVal;
      gconf_log(GCL_DEBUG, "Unknown type in %s", __FUNCTION__);
      break;
    }
}

ConfigValue*  
corba_value_from_gconf_value(GConfValue* value)
{
  ConfigValue* cv;

  cv = ConfigValue__alloc();

  fill_corba_value_from_gconf_value(value, cv);

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
fill_corba_schema_from_gconf_schema(GConfSchema* sc, 
                                     ConfigSchema* cs)
{
  switch (sc->type)
    {
    case GCONF_VALUE_INT:
      cs->value_type = IntVal;
      break;
    case GCONF_VALUE_BOOL:
      cs->value_type = BoolVal;
      break;
    case GCONF_VALUE_FLOAT:
      cs->value_type = FloatVal;
      break;
    case GCONF_VALUE_INVALID:
      cs->value_type = InvalidVal;
      break;
    case GCONF_VALUE_STRING:
      cs->value_type = StringVal;
      break;
    case GCONF_VALUE_SCHEMA:
      cs->value_type = SchemaVal;
      break;
    case GCONF_VALUE_LIST:
      cs->value_type = ListVal;
      break;
    case GCONF_VALUE_PAIR:
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
corba_schema_from_gconf_schema(GConfSchema* sc)
{
  ConfigSchema* cs;

  cs = ConfigSchema__alloc();

  fill_corba_schema_from_gconf_schema(sc, cs);

  return cs;
}

GConfSchema*  
gconf_schema_from_corba_schema(const ConfigSchema* cs)
{
  GConfSchema* sc;
  GConfValueType type = GCONF_VALUE_INVALID;

  switch (cs->value_type)
    {
    case InvalidVal:
      break;
    case StringVal:
      type = GCONF_VALUE_STRING;
      break;
    case IntVal:
      type = GCONF_VALUE_INT;
      break;
    case FloatVal:
      type = GCONF_VALUE_FLOAT;
      break;
    case SchemaVal:
      type = GCONF_VALUE_SCHEMA;
      break;
    case BoolVal:
      type = GCONF_VALUE_BOOL;
      break;
    case ListVal:
      type = GCONF_VALUE_LIST;
      break;
    case PairVal:
      type = GCONF_VALUE_PAIR;
      break;
    default:
      g_assert_not_reached();
      break;
    }

  sc = gconf_schema_new();

  gconf_schema_set_type(sc, type);

  gconf_schema_set_short_desc(sc, cs->short_desc);
  gconf_schema_set_long_desc(sc, cs->long_desc);
  gconf_schema_set_owner(sc, cs->owner);

  return sc;
}

const gchar* 
gconf_value_type_to_string(GConfValueType type)
{
  switch (type)
    {
    case GCONF_VALUE_INT:
      return "int";
      break;
    case GCONF_VALUE_STRING:
      return "string";
      break;
    case GCONF_VALUE_FLOAT:
      return "float";
      break;
    case GCONF_VALUE_BOOL:
      return "bool";
      break;
    case GCONF_VALUE_SCHEMA:
      return "schema";
      break;
    case GCONF_VALUE_LIST:
      return "list";
      break;
    case GCONF_VALUE_PAIR:
      return "pair";
      break;
    case GCONF_VALUE_IGNORE_SUBSEQUENT:
      return "ignore_subseq";
      break;
    default:
      g_assert_not_reached();
      return NULL; /* for warnings */
      break;
    }
}

GConfValueType 
gconf_value_type_from_string(const gchar* type_str)
{
  if (strcmp(type_str, "int") == 0)
    return GCONF_VALUE_INT;
  else if (strcmp(type_str, "float") == 0)
    return GCONF_VALUE_FLOAT;
  else if (strcmp(type_str, "string") == 0)
    return GCONF_VALUE_STRING;
  else if (strcmp(type_str, "bool") == 0)
    return GCONF_VALUE_BOOL;
  else if (strcmp(type_str, "schema") == 0)
    return GCONF_VALUE_SCHEMA;
  else if (strcmp(type_str, "list") == 0)
    return GCONF_VALUE_LIST;
  else if (strcmp(type_str, "pair") == 0)
    return GCONF_VALUE_PAIR;
  else if (strcmp(type_str, "ignore_subseq") == 0)
    return GCONF_VALUE_IGNORE_SUBSEQUENT;
  else
    return GCONF_VALUE_INVALID;
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
gconf_load_source_path(const gchar* filename, GConfError** err)
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
      if (err)
        *err = gconf_error_new(GCONF_FAILED,
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

          included = gconf_load_source_path(unq, NULL);

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
              gconf_log(GCL_INFO, _("Adding source `%s'\n"), varsubst);
              l = g_slist_prepend(l, g_strdup(varsubst));
            }
          g_free(varsubst);
        }
    }

  if (ferror(f))
    {
      /* This should basically never happen */
      if (err)
        *err = gconf_error_new(GCONF_FAILED,
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
gconf_concat_key_and_dir(const gchar* dir, const gchar* key)
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
gconf_string_to_gulong(const gchar* str)
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
gconf_log(GConfLogPriority pri, const gchar* fmt, ...)
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

  g_free(msg);
}
