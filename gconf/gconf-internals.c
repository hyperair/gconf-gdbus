
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

#include <config.h>
#include "gconf-internals.h"
#include "gconf-backend.h"
#include "gconf-schema.h"
#include <orb/orbit.h>
#include <liboaf/liboaf.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <locale.h>
#include <time.h>
#include <math.h>


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

static gboolean gconf_daemon_mode = FALSE;
static gchar* daemon_ior = NULL;

void
gconf_set_daemon_mode(gboolean setting)
{
  gconf_daemon_mode = setting;
}

gboolean
gconf_in_daemon_mode(void)
{
  return gconf_daemon_mode;
}

void
gconf_set_daemon_ior(const gchar* ior)
{
  if (daemon_ior != NULL)
    {
      g_free(daemon_ior);
      daemon_ior = NULL;
    }
      
  if (ior != NULL)
    daemon_ior = g_strdup(ior);
}

const gchar*
gconf_get_daemon_ior(void)
{
  return daemon_ior;
}

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

  g_assert(GCONF_VALUE_TYPE_VALID(type));
  
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

        if (gconf_value_list_type(gval) != GCONF_VALUE_INVALID)
          {
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
                
                if (val == NULL)
                  gconf_log(GCL_ERR, _("Couldn't interpret CORBA value for list element"));
                else if (val->type != gconf_value_list_type(gval))
                  gconf_log(GCL_ERR, _("Incorrect type for list element in %s"), __FUNCTION__);
                else
                  list = g_slist_prepend(list, val);
                
                ++i;
              }
        
            list = g_slist_reverse(list);
            
            gconf_value_set_list_nocopy(gval, list);
          }
        else
          {
            gconf_log(GCL_ERR, _("Received list from gconfd with a bad list type"));
          }
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
      cv->_u.string_value = CORBA_string_dup((char*)gconf_value_string(value));
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
            gconf_log(GCL_DEBUG, "Invalid list type in %s", __FUNCTION__);
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

gchar*
gconf_object_to_string (CORBA_Object obj,
                        GError **err)
{
  CORBA_Environment ev;
  gchar *ior;
  gchar *retval;
  
  CORBA_exception_init (&ev);

  ior = CORBA_ORB_object_to_string (oaf_orb_get (), obj, &ev);

  if (ior == NULL)
    {
      gconf_set_error (err,
                       GCONF_ERROR_FAILED,
                       _("Failed to convert object to IOR"));

      return NULL;
    }

  retval = g_strdup (ior);

  CORBA_free (ior);

  return retval;
}

static ConfigValueType
corba_type_from_gconf_type(GConfValueType type)
{
  switch (type)
    {
    case GCONF_VALUE_INT:
      return IntVal;
    case GCONF_VALUE_BOOL:
      return BoolVal;
    case GCONF_VALUE_FLOAT:
      return FloatVal;
    case GCONF_VALUE_INVALID:
      return InvalidVal;
    case GCONF_VALUE_STRING:
      return StringVal;
    case GCONF_VALUE_SCHEMA:
      return SchemaVal;
    case GCONF_VALUE_LIST:
      return ListVal;
    case GCONF_VALUE_PAIR:
      return PairVal;
    default:
      g_assert_not_reached();
      return InvalidVal;
    }
}

static GConfValueType
gconf_type_from_corba_type(ConfigValueType type)
{
  switch (type)
    {
    case InvalidVal:
      return GCONF_VALUE_INVALID;
    case StringVal:
      return GCONF_VALUE_STRING;
    case IntVal:
      return GCONF_VALUE_INT;
    case FloatVal:
      return GCONF_VALUE_FLOAT;
    case SchemaVal:
      return GCONF_VALUE_SCHEMA;
    case BoolVal:
      return GCONF_VALUE_BOOL;
    case ListVal:
      return GCONF_VALUE_LIST;
    case PairVal:
      return GCONF_VALUE_PAIR;
    default:
      g_assert_not_reached();
      return GCONF_VALUE_INVALID;
    }
}

void          
fill_corba_schema_from_gconf_schema(GConfSchema* sc, 
                                     ConfigSchema* cs)
{
  cs->value_type = corba_type_from_gconf_type(sc->type);
  cs->value_list_type = corba_type_from_gconf_type(sc->list_type);
  cs->value_car_type = corba_type_from_gconf_type(sc->car_type);
  cs->value_cdr_type = corba_type_from_gconf_type(sc->cdr_type);

  cs->locale = CORBA_string_dup(sc->locale ? sc->locale : "");
  cs->short_desc = CORBA_string_dup(sc->short_desc ? sc->short_desc : "");
  cs->long_desc = CORBA_string_dup(sc->long_desc ? sc->long_desc : "");
  cs->owner = CORBA_string_dup(sc->owner ? sc->owner : "");

  {
    gchar* encoded;
    GConfValue* default_val;

    default_val = gconf_schema_default_value(sc);

    if (default_val)
      {
        encoded = gconf_value_encode(default_val);

        g_assert(encoded != NULL);

        cs->encoded_default_value = CORBA_string_dup(encoded);

        g_free(encoded);
      }
    else
      cs->encoded_default_value = CORBA_string_dup("");
  }
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
  GConfValueType list_type = GCONF_VALUE_INVALID;
  GConfValueType car_type = GCONF_VALUE_INVALID;
  GConfValueType cdr_type = GCONF_VALUE_INVALID;

  type = gconf_type_from_corba_type(cs->value_type);
  list_type = gconf_type_from_corba_type(cs->value_list_type);
  car_type = gconf_type_from_corba_type(cs->value_car_type);
  cdr_type = gconf_type_from_corba_type(cs->value_cdr_type);

  sc = gconf_schema_new();

  gconf_schema_set_type(sc, type);
  gconf_schema_set_list_type(sc, list_type);
  gconf_schema_set_car_type(sc, car_type);
  gconf_schema_set_cdr_type(sc, cdr_type);

  if (*cs->locale != '\0')
    gconf_schema_set_locale(sc, cs->locale);

  if (*cs->short_desc != '\0')
    gconf_schema_set_short_desc(sc, cs->short_desc);

  if (*cs->long_desc != '\0')
    gconf_schema_set_long_desc(sc, cs->long_desc);

  if (*cs->owner != '\0')
    gconf_schema_set_owner(sc, cs->owner);

  {
    GConfValue* val;

    val = gconf_value_decode(cs->encoded_default_value);

    if (val)
      gconf_schema_set_default_value_nocopy(sc, val);
  }
  
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
    case GCONF_VALUE_INVALID:
      return "*invalid*";
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
gconf_load_source_path(const gchar* filename, GError** err)
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
        *err = gconf_error_new(GCONF_ERROR_FAILED,
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
        *err = gconf_error_new(GCONF_ERROR_FAILED,
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

gboolean
gconf_string_to_double(const gchar* str, gdouble* retloc)
{
  int res;
  char *old_locale;

  /* make sure we write values to files in a consistent manner */
  old_locale = g_strdup (setlocale (LC_NUMERIC, NULL));
  setlocale (LC_NUMERIC, "C");

  *retloc = 0.0;
  res = sscanf (str, "%lf", retloc);

  setlocale (LC_NUMERIC, old_locale);
  g_free (old_locale);

  if (res == 1)
    return TRUE;
  else
    return FALSE;
}

gchar*
gconf_double_to_string(gdouble val)
{
  char str[101 + DBL_DIG];
  char *old_locale;

  /* make sure we write values to files in a consistent manner */
  old_locale = g_strdup (setlocale (LC_NUMERIC, NULL));
  setlocale (LC_NUMERIC, "C");
  
  if (fabs (val) < 1e9 && fabs (val) > 1e-5)
    snprintf (str, 100 + DBL_DIG, "%.*g", DBL_DIG, val);
  else
    snprintf (str, 100 + DBL_DIG, "%f", val);

  setlocale (LC_NUMERIC, old_locale);
  g_free (old_locale);
  
  return g_strdup(str);
}

const gchar*
gconf_current_locale(void)
{
#ifdef HAVE_LC_MESSAGES
  return setlocale(LC_MESSAGES, NULL);
#else
  return setlocale(LC_CTYPE, NULL);
#endif
}

/*
 * Log
 */

gchar*
gconf_quote_percents(const gchar* src)
{
  gchar* dest;
  const gchar* s;
  gchar* d;

  g_return_val_if_fail(src != NULL, NULL);
  
  /* waste memory! woo-hoo! */
  dest = g_malloc0(strlen(src)*2+4);
  
  d = dest;
  
  s = src;
  while (*s)
    {
      switch (*s)
        {
        case '%':
          {
            *d = '%';
            ++d;
            *d = '%';
            ++d;
          }
          break;
          
        default:
          {
            *d = *s;
            ++d;
          }
          break;
        }
      ++s;
    }

  /* End with NULL */
  *d = '\0';
  
  return dest;
}

#include <syslog.h>

void
gconf_log(GConfLogPriority pri, const gchar* fmt, ...)
{
  gchar* msg;
  va_list args;
  int syslog_pri = LOG_DEBUG;

#ifndef GCONF_ENABLE_DEBUG
  if (pri == GCL_DEBUG)
    return;
#endif
  
  va_start (args, fmt);
  msg = g_strdup_vprintf(fmt, args);
  va_end (args);

  if (gconf_daemon_mode)
    {
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

      syslog(syslog_pri, "%s", msg);
    }
  else
    {
      switch (pri)
        {
        case GCL_EMERG:
        case GCL_ALERT:
        case GCL_CRIT:
        case GCL_ERR:
        case GCL_WARNING:
          fprintf(stderr, "%s\n", msg);
          break;
      
        case GCL_NOTICE:
        case GCL_INFO:
        case GCL_DEBUG:
          printf("%s\n", msg);
          break;

        default:
          g_assert_not_reached();
          break;
        }
    }
  
  g_free(msg);
}

/*
 * List/pair conversion
 */

GConfValue*
gconf_value_list_from_primitive_list(GConfValueType list_type, GSList* list)
{
  GSList* value_list;
  GSList* tmp;

  g_return_val_if_fail(list_type != GCONF_VALUE_INVALID, NULL);
  g_return_val_if_fail(list_type != GCONF_VALUE_LIST, NULL);
  g_return_val_if_fail(list_type != GCONF_VALUE_PAIR, NULL);
  
  value_list = NULL;

  tmp = list;

  while (tmp != NULL)
    {
      GConfValue* val;

      val = gconf_value_new(list_type);

      switch (list_type)
        {
        case GCONF_VALUE_INT:
          gconf_value_set_int(val, GPOINTER_TO_INT(tmp->data));
          break;

        case GCONF_VALUE_BOOL:
          gconf_value_set_bool(val, GPOINTER_TO_INT(tmp->data));
          break;

        case GCONF_VALUE_FLOAT:
          gconf_value_set_float(val, *((gdouble*)tmp->data));
          break;

        case GCONF_VALUE_STRING:
          gconf_value_set_string(val, tmp->data);
          break;

        case GCONF_VALUE_SCHEMA:
          gconf_value_set_schema(val, tmp->data);
          break;
          
        default:
          g_assert_not_reached();
          break;
        }

      value_list = g_slist_prepend(value_list, val);

      tmp = g_slist_next(tmp);
    }

  /* Get it in the right order. */
  value_list = g_slist_reverse(value_list);

  {
    GConfValue* value_with_list;
    
    value_with_list = gconf_value_new(GCONF_VALUE_LIST);
    gconf_value_set_list_type(value_with_list, list_type);
    gconf_value_set_list_nocopy(value_with_list, value_list);

    return value_with_list;
  }
}


static GConfValue*
from_primitive(GConfValueType type, gconstpointer address)
{
  GConfValue* val;

  val = gconf_value_new(type);

  switch (type)
    {
    case GCONF_VALUE_INT:
      gconf_value_set_int(val, *((const gint*)address));
      break;

    case GCONF_VALUE_BOOL:
      gconf_value_set_bool(val, *((const gboolean*)address));
      break;

    case GCONF_VALUE_STRING:
      gconf_value_set_string(val, *((const gchar**)address));
      break;

    case GCONF_VALUE_FLOAT:
      gconf_value_set_float(val, *((const gdouble*)address));
      break;

    case GCONF_VALUE_SCHEMA:
      gconf_value_set_schema(val, *((GConfSchema**)address));
      break;
      
    default:
      g_assert_not_reached();
      break;
    }

  return val;
}

GConfValue*
gconf_value_pair_from_primitive_pair(GConfValueType car_type,
                                     GConfValueType cdr_type,
                                     gconstpointer address_of_car,
                                     gconstpointer address_of_cdr)
{
  GConfValue* pair;
  GConfValue* car;
  GConfValue* cdr;
  
  g_return_val_if_fail(car_type != GCONF_VALUE_INVALID, NULL);
  g_return_val_if_fail(car_type != GCONF_VALUE_LIST, NULL);
  g_return_val_if_fail(car_type != GCONF_VALUE_PAIR, NULL);
  g_return_val_if_fail(cdr_type != GCONF_VALUE_INVALID, NULL);
  g_return_val_if_fail(cdr_type != GCONF_VALUE_LIST, NULL);
  g_return_val_if_fail(cdr_type != GCONF_VALUE_PAIR, NULL);
  g_return_val_if_fail(address_of_car != NULL, NULL);
  g_return_val_if_fail(address_of_cdr != NULL, NULL);
  
  car = from_primitive(car_type, address_of_car);
  cdr = from_primitive(cdr_type, address_of_cdr);

  pair = gconf_value_new(GCONF_VALUE_PAIR);
  gconf_value_set_car_nocopy(pair, car);
  gconf_value_set_cdr_nocopy(pair, cdr);

  return pair;
}


GSList*
gconf_value_list_to_primitive_list_destructive(GConfValue* val,
                                               GConfValueType list_type,
                                               GError** err)
{
  GSList* retval;

  g_return_val_if_fail(val != NULL, NULL);
  g_return_val_if_fail(list_type != GCONF_VALUE_INVALID, NULL);
  g_return_val_if_fail(list_type != GCONF_VALUE_LIST, NULL);
  g_return_val_if_fail(list_type != GCONF_VALUE_PAIR, NULL);
  g_return_val_if_fail(err == NULL || *err == NULL, NULL);
  
  if (val->type != GCONF_VALUE_LIST)
    {
      if (err)
        *err = gconf_error_new(GCONF_ERROR_TYPE_MISMATCH,
                               _("Expected list, got %s"),
                               gconf_value_type_to_string(val->type));
      gconf_value_free(val);
      return NULL;
    }

  if (gconf_value_list_type(val) != list_type)
    {
      if (err)
        *err = gconf_error_new(GCONF_ERROR_TYPE_MISMATCH,
                               _("Expected list of %s, got list of %s"),
                               gconf_value_type_to_string(list_type),
                               gconf_value_type_to_string(val->type));
      gconf_value_free(val);
      return NULL;
    }

  g_assert(gconf_value_list_type(val) == list_type);
      
  retval = gconf_value_list(val);

  /* Cheating the API to avoid a list copy; set this to NULL to
         avoid destroying the list */
  val->d.list_data.list = NULL;
      
  gconf_value_free(val);
  val = NULL;
      
  {
    /* map (typeChange, retval) */
    GSList* tmp;

    tmp = retval;

    while (tmp != NULL)
      {
        GConfValue* elem = tmp->data;

        g_assert(elem != NULL);
        g_assert(elem->type == list_type);
            
        switch (list_type)
          {
          case GCONF_VALUE_INT:
          case GCONF_VALUE_BOOL:
            tmp->data = GINT_TO_POINTER(gconf_value_int(elem));
            break;
                
          case GCONF_VALUE_FLOAT:
            {
              gdouble* d = g_new(gdouble, 1);
              *d = gconf_value_float(elem);
              tmp->data = d;
            }
            break;

          case GCONF_VALUE_STRING:
            {
              /* Cheat again, and steal the string from the value */
              tmp->data = elem->d.string_data;
              elem->d.string_data = NULL;
            }
            break;

          case GCONF_VALUE_SCHEMA:
            {
              /* and also steal the schema... */
              tmp->data = elem->d.schema_data;
              elem->d.schema_data = NULL;
            }
            break;
                
          default:
            g_assert_not_reached();
            break;
          }

        /* Clean up the value */
        gconf_value_free(elem);
            
        tmp = g_slist_next(tmp);
      }
  } /* list conversion block */
      
  return retval;
}


static void
primitive_value(gpointer retloc, GConfValue* val)
{
  switch (val->type)
    {
    case GCONF_VALUE_INT:
      *((gint*)retloc) = gconf_value_int(val);
      break;

    case GCONF_VALUE_FLOAT:
      *((gdouble*)retloc) = gconf_value_float(val);
      break;

    case GCONF_VALUE_STRING:
      {
        *((gchar**)retloc) = val->d.string_data;
        /* cheat and steal the string to avoid a copy */
        val->d.string_data = NULL;
      }
      break;

    case GCONF_VALUE_BOOL:
      *((gboolean*)retloc) = gconf_value_bool(val);
      break;

    case GCONF_VALUE_SCHEMA:
      *((GConfSchema**)retloc) = gconf_value_schema(val);
      break;
      
    default:
      g_assert_not_reached();
      break;
    }
}

gboolean
gconf_value_pair_to_primitive_pair_destructive(GConfValue* val,
                                               GConfValueType car_type,
                                               GConfValueType cdr_type,
                                               gpointer car_retloc,
                                               gpointer cdr_retloc,
                                               GError** err)
{
  GConfValue* car;
  GConfValue* cdr;

  g_return_val_if_fail(val != NULL, FALSE);
  g_return_val_if_fail(car_type != GCONF_VALUE_INVALID, FALSE);
  g_return_val_if_fail(car_type != GCONF_VALUE_LIST, FALSE);
  g_return_val_if_fail(car_type != GCONF_VALUE_PAIR, FALSE);
  g_return_val_if_fail(cdr_type != GCONF_VALUE_INVALID, FALSE);
  g_return_val_if_fail(cdr_type != GCONF_VALUE_LIST, FALSE);
  g_return_val_if_fail(cdr_type != GCONF_VALUE_PAIR, FALSE);
  g_return_val_if_fail(car_retloc != NULL, FALSE);
  g_return_val_if_fail(cdr_retloc != NULL, FALSE);
  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);  
      
  if (val->type != GCONF_VALUE_PAIR)
    {
      if (err)
        *err = gconf_error_new(GCONF_ERROR_TYPE_MISMATCH,
                               _("Expected pair, got %s"),
                               gconf_value_type_to_string(val->type));
      gconf_value_free(val);
      return FALSE;
    }

  car = gconf_value_car(val);
  cdr = gconf_value_cdr(val);
      
  if (car == NULL ||
      cdr == NULL)
    {
      if (err)
        *err = gconf_error_new(GCONF_ERROR_TYPE_MISMATCH, 
                               _("Expected (%s,%s) pair, got a pair with one or both values missing"),
                               gconf_value_type_to_string(car_type),
                               gconf_value_type_to_string(cdr_type));

      gconf_value_free(val);
      return FALSE;
    }

  g_assert(car != NULL);
  g_assert(cdr != NULL);
      
  if (car->type != car_type ||
      cdr->type != cdr_type)
    {
      if (err)
        *err = gconf_error_new(GCONF_ERROR_TYPE_MISMATCH,
                               _("Expected pair of type (%s,%s) got type (%s,%s)"),
                               gconf_value_type_to_string(car_type),
                               gconf_value_type_to_string(cdr_type),
                               gconf_value_type_to_string(car->type),
                               gconf_value_type_to_string(cdr->type));
      gconf_value_free(val);
      return FALSE;
    }

  primitive_value(car_retloc, car);
  primitive_value(cdr_retloc, cdr);

  gconf_value_free(val);

  return TRUE;
}



/*
 * Encode/decode
 */

gchar*
gconf_quote_string   (const gchar* src)
{
  gchar* dest;
  const gchar* s;
  gchar* d;

  g_return_val_if_fail(src != NULL, NULL);
  
  /* waste memory! woo-hoo! */
  dest = g_malloc0(strlen(src)*2+4);
  
  d = dest;

  *d = '"';
  ++d;
  
  s = src;
  while (*s)
    {
      switch (*s)
        {
        case '"':
          {
            *d = '\\';
            ++d;
            *d = '"';
            ++d;
          }
          break;
          
        case '\\':
          {
            *d = '\\';
            ++d;
            *d = '\\';
            ++d;
          }
          break;
          
        default:
          {
            *d = *s;
            ++d;
          }
          break;
        }
      ++s;
    }

  /* End with quote mark and NULL */
  *d = '"';
  ++d;
  *d = '\0';
  
  return dest;
}

gchar*
gconf_unquote_string (const gchar* str, const gchar** end, GError** err)
{
  gchar* unq;
  gchar* unq_end = NULL;

  g_return_val_if_fail(end != NULL, NULL);
  g_return_val_if_fail(err == NULL || *err == NULL, NULL);
  g_return_val_if_fail(str != NULL, NULL);
  
  unq = g_strdup(str);

  gconf_unquote_string_inplace(unq, &unq_end, err);

  *end = (str + (unq_end - unq));

  return unq;
}

void
gconf_unquote_string_inplace (gchar* str, gchar** end, GError** err)
{
  gchar* dest;
  gchar* s;

  g_return_if_fail(end != NULL);
  g_return_if_fail(err == NULL || *err == NULL);
  g_return_if_fail(str != NULL);
  
  dest = s = str;

  if (*s != '"')
    {
      if (err)
        *err = gconf_error_new(GCONF_ERROR_PARSE_ERROR,
                               _("Quoted string doesn't begin with a quotation mark"));
      *end = str;
      return;
    }

  /* Skip the initial quote mark */
  ++s;
  
  while (*s)
    {
      g_assert(s > dest); /* loop invariant */
      
      switch (*s)
        {
        case '"':
          /* End of the string, return now */
          *dest = '\0';
          ++s;
          *end = s;
          return;
          break;

        case '\\':
          /* Possible escaped quote or \ */
          ++s;
          if (*s == '"')
            {
              *dest = *s;
              ++s;
              ++dest;
            }
          else if (*s == '\\')
            {
              *dest = *s;
              ++s;
              ++dest;
            }
          else
            {
              /* not an escaped char */
              *dest = '\\';
              ++dest;
              /* ++s already done. */
            }
          break;

        default:
          *dest = *s;
          ++dest;
          ++s;
          break;
        }

      g_assert(s > dest); /* loop invariant */
    }
  
  /* If we reach here this means the close quote was never encountered */

  *dest = '\0';
  
  if (err)
    *err = gconf_error_new(GCONF_ERROR_PARSE_ERROR,
                           _("Quoted string doesn't end with a quotation mark"));
  *end = s;
  return;
}

/* The encoding format

   The first byte of the encoded string is the type of the value:

    i  int
    b  bool
    f  float
    s  string
    c  schema
    p  pair
    l  list
    v  invalid

    For int, the rest of the encoded value is the integer to be parsed with atoi()
    For bool, the rest is 't' or 'f'
    For float, the rest is a float to parse with g_strtod()
    For string, the rest is the string (not quoted)
    For schema, the encoding is complicated; see below.
    For pair, the rest is two primitive encodings (ibcfs), quoted, separated by a comma,
              car before cdr
    For list, first character is type, the rest is primitive encodings, quoted,
              separated by commas

    Schema:

    After the 'c' indicating schema, the second character is a byte indicating
    the type the schema expects. Then a comma, and the quoted locale, or "" for none.
    comma, and quoted short description; comma, quoted long description; comma, default
    value in the encoded format given above, quoted.
*/

static gchar type_byte(GConfValueType type)
{
  switch (type)
    {
    case GCONF_VALUE_INT:
      return 'i';
      break;
        
    case GCONF_VALUE_BOOL:
      return 'b';
      break;

    case GCONF_VALUE_FLOAT:
      return 'f';
      break;

    case GCONF_VALUE_STRING:
      return 's';
      break;

    case GCONF_VALUE_SCHEMA:
      return 'c';
      break;

    case GCONF_VALUE_LIST:
      return 'l';
      break;

    case GCONF_VALUE_PAIR:
      return 'p';
      break;

    case GCONF_VALUE_INVALID:
      return 'v';
      break;
      
    default:
      g_assert_not_reached();
      return '\0';
      break;
    }
}

GConfValueType
byte_type(gchar byte)
{
  switch (byte)
    {
    case 'i':
      return GCONF_VALUE_INT;
      break;

    case 'b':
      return GCONF_VALUE_BOOL;
      break;

    case 's':
      return GCONF_VALUE_STRING;
      break;

    case 'c':
      return GCONF_VALUE_SCHEMA;
      break;

    case 'f':
      return GCONF_VALUE_FLOAT;
      break;

    case 'l':
      return GCONF_VALUE_LIST;
      break;

    case 'p':
      return GCONF_VALUE_PAIR;
      break;
      
    case 'v':
      return GCONF_VALUE_INVALID;
      break;

    default:
      return GCONF_VALUE_INVALID;
      break;
    }
}

GConfValue*
gconf_value_decode (const gchar* encoded)
{
  GConfValueType type;
  GConfValue* val;
  const gchar* s;
  
  type = byte_type(*encoded);

  if (type == GCONF_VALUE_INVALID)
    return NULL;

  val = gconf_value_new(type);

  s = encoded + 1;
  
  switch (val->type)
    {
    case GCONF_VALUE_INT:
      gconf_value_set_int(val, atoi(s));
      break;
        
    case GCONF_VALUE_BOOL:
      gconf_value_set_bool(val, *s == 't' ? TRUE : FALSE);
      break;

    case GCONF_VALUE_FLOAT:
      {
        double d;
        gchar* endptr = NULL;
        
        d = g_strtod(s, &endptr);
        if (endptr == s)
          g_warning("Failure converting string to double in %s", __FUNCTION__);
        gconf_value_set_float(val, d);
      }
      break;

    case GCONF_VALUE_STRING:
      {
        gconf_value_set_string(val, s);
      }
      break;

    case GCONF_VALUE_SCHEMA:
      {
        GConfSchema* sc = gconf_schema_new();
        const gchar* end = NULL;
        gchar* unquoted;
        
        gconf_value_set_schema(val, sc);

        gconf_schema_set_type(sc, byte_type(*s));
        ++s;
        gconf_schema_set_list_type(sc, byte_type(*s));
        ++s;
        gconf_schema_set_car_type(sc, byte_type(*s));
        ++s;
        gconf_schema_set_cdr_type(sc, byte_type(*s));
        ++s;

        /* locale */
        unquoted = gconf_unquote_string(s, &end, NULL);

        gconf_schema_set_locale(sc, unquoted);

        g_free(unquoted);
        
        if (*end != ',')
          g_warning("no comma after locale in schema");

        ++end;
        s = end;

        /* short */
        unquoted = gconf_unquote_string(s, &end, NULL);

        gconf_schema_set_short_desc(sc, unquoted);

        g_free(unquoted);
        
        if (*end != ',')
          g_warning("no comma after short desc in schema");

        ++end;
        s = end;


        /* long */
        unquoted = gconf_unquote_string(s, &end, NULL);

        gconf_schema_set_long_desc(sc, unquoted);

        g_free(unquoted);
        
        if (*end != ',')
          g_warning("no comma after long desc in schema");

        ++end;
        s = end;
        
        
        /* default value */
        unquoted = gconf_unquote_string(s, &end, NULL);

        gconf_schema_set_default_value_nocopy(sc, gconf_value_decode(unquoted));

        g_free(unquoted);
        
        if (*end != '\0')
          g_warning("trailing junk after encoded schema");
      }
      break;

    case GCONF_VALUE_LIST:
      {
        GSList* value_list = NULL;

        gconf_value_set_list_type(val, byte_type(*s));
	++s;

        while (*s)
          {
            gchar* unquoted;
            const gchar* end;
            
            GConfValue* elem;
            
            unquoted = gconf_unquote_string(s, &end, NULL);            

            elem = gconf_value_decode(unquoted);

            g_free(unquoted);
            
            if (elem)
              value_list = g_slist_prepend(value_list, elem);
            
            s = end;
            if (*s == ',')
              ++s;
            else if (*s != '\0')
              {
                g_warning("weird character in encoded list");
                break; /* error */
              }
          }

        value_list = g_slist_reverse(value_list);

        gconf_value_set_list_nocopy(val, value_list);
      }
      break;

    case GCONF_VALUE_PAIR:
      {
        gchar* unquoted;
        const gchar* end;
        
        GConfValue* car;
        GConfValue* cdr;
        
        unquoted = gconf_unquote_string(s, &end, NULL);            
        
        car = gconf_value_decode(unquoted);

        g_free(unquoted);
        
        s = end;
        if (*s == ',')
          ++s;
        else
          {
            g_warning("weird character in encoded pair");
          }
        
        unquoted = gconf_unquote_string(s, &end, NULL);
        
        cdr = gconf_value_decode(unquoted);
        g_free(unquoted);


        gconf_value_set_car_nocopy(val, car);
        gconf_value_set_cdr_nocopy(val, cdr);
      }
      break;

    default:
      g_assert_not_reached();
      break;
    }

  return val;
}

gchar*
gconf_value_encode (GConfValue* val)
{
  gchar* retval = NULL;
  
  g_return_val_if_fail(val != NULL, NULL);

  switch (val->type)
    {
    case GCONF_VALUE_INT:
      retval = g_strdup_printf("i%d", gconf_value_int(val));
      break;
        
    case GCONF_VALUE_BOOL:
      retval = g_strdup_printf("b%c", gconf_value_bool(val) ? 't' : 'f');
      break;

    case GCONF_VALUE_FLOAT:
      retval = g_strdup_printf("f%g", gconf_value_float(val));
      break;

    case GCONF_VALUE_STRING:
      retval = g_strdup_printf("s%s", gconf_value_string(val));
      break;

    case GCONF_VALUE_SCHEMA:
      {
        gchar* tmp;
        gchar* retval;
        gchar* quoted;
        gchar* encoded;
        GConfSchema* sc;

        sc = gconf_value_schema(val);
        
        tmp = g_strdup_printf("c%c%c%c%c,",
			      type_byte(gconf_schema_type(sc)),
			      type_byte(gconf_schema_list_type(sc)),
			      type_byte(gconf_schema_car_type(sc)),
			      type_byte(gconf_schema_cdr_type(sc)));

        quoted = gconf_quote_string(gconf_schema_locale(sc) ?
                                    gconf_schema_locale(sc) : "");
        retval = g_strconcat(tmp, quoted, ",", NULL);

        g_free(tmp);
        g_free(quoted);

        tmp = retval;
        quoted = gconf_quote_string(gconf_schema_short_desc(sc) ?
                                    gconf_schema_short_desc(sc) : "");

        retval = g_strconcat(tmp, quoted, ",", NULL);

        g_free(tmp);
        g_free(quoted);


        tmp = retval;
        quoted = gconf_quote_string(gconf_schema_long_desc(sc) ?
                                    gconf_schema_long_desc(sc) : "");

        retval = g_strconcat(tmp, quoted, ",", NULL);

        g_free(tmp);
        g_free(quoted);
        

        if (gconf_schema_default_value(sc) != NULL)
          encoded = gconf_value_encode(gconf_schema_default_value(sc));
        else
          encoded = g_strdup("");

        tmp = retval;
          
        quoted = gconf_quote_string(encoded);

        retval = g_strconcat(tmp, quoted, NULL);

        g_free(tmp);
        g_free(quoted);
        g_free(encoded);
      }
      break;

    case GCONF_VALUE_LIST:
      {
        GSList* tmp;

        retval = g_strdup_printf("l%c", type_byte(gconf_value_list_type(val)));
        
        tmp = gconf_value_list(val);

        while (tmp != NULL)
          {
            GConfValue* elem = tmp->data;
            gchar* encoded;
            gchar* quoted;
            
            g_assert(elem != NULL);

            encoded = gconf_value_encode(elem);

            quoted = gconf_quote_string(encoded);

            g_free(encoded);

            {
              gchar* free_me;
              free_me = retval;
              
              retval = g_strconcat(retval, ",", quoted, NULL);
              
              g_free(quoted);
              g_free(free_me);
            }
            
            tmp = g_slist_next(tmp);
          }
      }
      break;

    case GCONF_VALUE_PAIR:
      {
        gchar* car_encoded;
        gchar* cdr_encoded;
        gchar* car_quoted;
        gchar* cdr_quoted;

        car_encoded = gconf_value_encode(gconf_value_car(val));
        cdr_encoded = gconf_value_encode(gconf_value_cdr(val));

        car_quoted = gconf_quote_string(car_encoded);
        cdr_quoted = gconf_quote_string(cdr_encoded);

        retval = g_strconcat("p", car_quoted, ",", cdr_quoted, NULL);

        g_free(car_encoded);
        g_free(cdr_encoded);
        g_free(car_quoted);
        g_free(cdr_quoted);
      }
      break;

    default:
      g_assert_not_reached();
      break;
      
    }

  return retval;
}

gboolean
gconf_handle_oaf_exception(CORBA_Environment* ev, GError** err)
{
  switch (ev->_major)
    {
    case CORBA_NO_EXCEPTION:
      CORBA_exception_free(ev);
      return FALSE;
      break;
    case CORBA_SYSTEM_EXCEPTION:
      if (err)
        *err = gconf_error_new(GCONF_ERROR_NO_SERVER, _("CORBA error: %s"),
                               CORBA_exception_id(ev));
      CORBA_exception_free(ev);
      return TRUE;
      break;

    case CORBA_USER_EXCEPTION:
      {
        const gchar* id = CORBA_exception_id(ev);

        if (strcmp(id, "IDL:OAF/GeneralError:1.0") == 0)
          {
            OAF_GeneralError* ge = CORBA_exception_value(ev);

            if (err)
              *err = gconf_error_new(GCONF_ERROR_OAF_ERROR, _("%s"), ge->description);
          }
        else if (strcmp(id,"IDL:OAF/ActivationContext/NotListed:1.0" ) == 0)
          {
            if (err)
              *err = gconf_error_new(GCONF_ERROR_OAF_ERROR, _("attempt to remove not-listed OAF object directory"));
          }
        else if (strcmp(id,"IDL:OAF/ActivationContext/AlreadyListed:1.0" ) == 0)
          {
            if (err)
              *err = gconf_error_new(GCONF_ERROR_OAF_ERROR, _("attempt to add already-listed OAF directory")); 
          }
        else if (strcmp(id,"IDL:OAF/ActivationContext/ParseFailed:1.0") == 0)
          {
            OAF_ActivationContext_ParseFailed* pe = CORBA_exception_value(ev);
            
            if (err)
              *err = gconf_error_new(GCONF_ERROR_OAF_ERROR, _("OAF parse error: %s"), pe->description);
          }
        else
          {
            if (err)
              *err = gconf_error_new(GCONF_ERROR_OAF_ERROR, _("Unknown OAF error"));
          }
        
        CORBA_exception_free(ev);
        return TRUE;
      }
      break;
    default:
      g_assert_not_reached();
      return TRUE;
      break;
    }
}

/* Sleep */

#ifdef HAVE_NANOSLEEP

void
gconf_nanosleep(gulong useconds)
{
  struct timespec ts={tv_sec: (long int)(useconds/1000000),
                      tv_nsec: (long int)(useconds%1000000)*1000ul};
  
  nanosleep(&ts,NULL);
}

#elif HAVE_USLEEP

void
gconf_nanosleep(gulong useconds)
{
  usleep(useconds);
}

#else
#error "need nanosleep or usleep right now (fix with simple select() hack)"
#endif

/*
 * Locks using directories, to work on NFS (at least potentially)
 */

struct _GConfLock {
  gchar* lock_directory;
};

static void
gconf_lock_destroy(GConfLock* lock)
{
  g_free(lock->lock_directory);
  g_free(lock);
}

GConfLock*
gconf_get_lock(const gchar* lock_directory,
               GError** err)
{
  GConfLock* lock;
  gboolean got_it = FALSE;
  gboolean error_occurred = FALSE;
  gboolean stale = FALSE;
  gchar* iorfile;
  
  g_return_val_if_fail(lock_directory != NULL, NULL);
  
  lock = g_new(GConfLock, 1);

  lock->lock_directory = g_strdup(lock_directory);

  iorfile = g_strconcat(lock->lock_directory, "/ior", NULL);
  
  if (mkdir(lock->lock_directory, 0700) < 0)
    {
      if (errno == EEXIST)
        {
          /* Check the current IOR file and ping its daemon */
          FILE* fp;
          
          fp = fopen(iorfile, "r");
          
          if (fp == NULL)
            {
              gconf_log(GCL_WARNING, _("No ior file in `%s'"),
                        lock->lock_directory);
              stale = TRUE;
              got_it = TRUE;
              goto out;
            }
          else
            {
              gchar buf[2048] = { '\0' };
              gchar* str = NULL;
              fgets(buf, 2047, fp);
              fclose(fp);

              /* The lockfile format is <pid>:<ior> for gconfd
                 or <pid>:none for gconftool */
              str = buf;
              while (isdigit(*str))
                ++str;

              if (*str == ':')
                ++str;
              
              if (str[0] == 'n' &&
                  str[1] == 'o' &&
                  str[2] == 'n' &&
                  str[3] == 'e')
                {
                  /* It's locked by gconftool, not a daemon;
                     the daemon always "wins" the lock, with a warning */
                  if (gconf_in_daemon_mode())
                    {
                      gconf_log(GCL_WARNING, _("gconfd taking lock `%s' from some other process"),
                                lock->lock_directory);
                      stale = TRUE;
                      got_it = TRUE;
                      goto out;
                    }
                  else
                    {
                      error_occurred = TRUE;
                      gconf_set_error(err,
                                      GCONF_ERROR_LOCK_FAILED,
                                      _("Another program has lock `%s'"),
                                      lock->lock_directory);
                      goto out;
                    }
                }
              else
                {
                  ConfigServer server;
                  CORBA_ORB orb;
                  CORBA_Environment ev;

                  CORBA_exception_init(&ev);
                  
                  orb = oaf_orb_get();

                  if (orb == NULL)
                    {
                      error_occurred = TRUE;
                      gconf_set_error(err,
                                      GCONF_ERROR_LOCK_FAILED,
                                      _("couldn't contact ORB to ping existing gconfd"));
                      goto out;
                    }
                  
                  server = CORBA_ORB_string_to_object(orb, str, &ev);

                  if (CORBA_Object_is_nil(server, &ev))
                    {
                      gconf_log(GCL_WARNING, _("Removing stale lock `%s' because IOR couldn't be converted to object reference, IOR `%s'"),
                                lock->lock_directory, str);
                      stale = TRUE;
                      got_it = TRUE;
                      goto out;
                    }
                  else
                    {
                      ConfigServer_ping(server, &ev);
      
                      if (ev._major != CORBA_NO_EXCEPTION)
                        {
                          gconf_log(GCL_WARNING, _("Removing stale lock `%s' because of error pinging server: %s"),
                                    lock->lock_directory, CORBA_exception_id(&ev));
                          CORBA_exception_free(&ev);
                          stale = TRUE;
                          got_it = TRUE;
                          goto out;
                        }
                      else
                        {
                          error_occurred = TRUE;
                          gconf_set_error(err,
                                          GCONF_ERROR_LOCK_FAILED,
                                          _("GConf configuration daemon (gconfd) has lock `%s'"),
                                          lock->lock_directory);
                          goto out;
                        }
                    }
                }
            }
        }
      else
        {
          /* give up */
          error_occurred = TRUE;
          gconf_set_error(err,
                          GCONF_ERROR_LOCK_FAILED,
                          _("couldn't create directory `%s': %s"),
                          lock->lock_directory, strerror(errno));
          goto out;
        }
    }
  else
    {
      got_it = TRUE;
    }
  
 out:

  if (error_occurred)
    {
      g_assert(!got_it);

      g_free(iorfile);
      gconf_lock_destroy(lock);
      return NULL;
    }

  g_assert(got_it);
  
  if (stale)
    unlink(iorfile);

  {
    int fd = -1;

    fd = open(iorfile, O_WRONLY | O_CREAT | O_TRUNC, 0700);

    if (fd < 0)
      {
        gconf_set_error(err,
                        GCONF_ERROR_LOCK_FAILED,
                        _("Can't create lock `%s': %s"),
                        iorfile, strerror(errno));
        g_free(iorfile);
        gconf_lock_destroy(lock);
        return NULL;
      }
    else
      {
        const gchar* ior;
        int retval;
        gchar* s;
        
        s = g_strdup_printf("%u:", (guint)getpid());
        
        retval = write(fd, s, strlen(s));

        g_free(s);
        
        if (retval >= 0)
          {
            ior = gconf_get_daemon_ior();
            
            if (ior == NULL)
              {
                retval = write(fd, "none", 4);
              }
            else
              {
                retval = write(fd, ior, strlen(ior));
              }
          }

        if (retval < 0)
          {
            gconf_set_error(err,
                            GCONF_ERROR_LOCK_FAILED,
                            _("Can't write to file `%s': %s"),
                            iorfile, strerror(errno));
            close(fd);
            g_free(iorfile);
            gconf_lock_destroy(lock);            
            return NULL;
          }

        if (close(fd) < 0)
          {
            gconf_set_error(err,
                            GCONF_ERROR_LOCK_FAILED,
                            _("Failed to close file `%s': %s"),
                            iorfile, strerror(errno));
            g_free(iorfile);
            gconf_lock_destroy(lock);            
            return NULL;
          }
      }
  }
  
  g_free(iorfile);
  return lock;
}

gboolean
gconf_release_lock(GConfLock* lock,
                   GError** err)
{
  gchar* iorfile;
  FILE* fp;
  gchar buf[256] = { '\0' };
  gchar* str = NULL;
  gulong pid;
  
  iorfile = g_strconcat(lock->lock_directory, "/ior", NULL);
          
  fp = fopen(iorfile, "r");
  
  if (fp == NULL)
    {
      gconf_set_error(err,
                      GCONF_ERROR_FAILED,
                      _("Can't open lock file `%s'; assuming it isn't ours: %s"),
                      iorfile, strerror(errno));
      g_free(iorfile);
      return FALSE;
    }

  fgets(buf, 255, fp);
  fclose(fp);

  /* The lockfile format is <pid>:<ior> for gconfd
     or <pid>:none for gconftool */

  /* get PID to see if it's ours */
  pid = strtoul(buf, &str, 10);

  if (buf == str)
    {
      gconf_log(GCL_WARNING, _("Corrupt lock file `%s', removing anyway"),
                iorfile);

    }
  else
    {
      if (pid != getpid())
        {
          gconf_set_error(err,
                          GCONF_ERROR_FAILED,
                          _("Didn't create lock file `%s' (creator pid %u, our pid %u; assuming someone took our lock"),
                          iorfile, (guint)pid, (guint)getpid());
          g_free(iorfile);
          return FALSE;
        }
    }
  
  /* Race condition here, someone could replace the lockfile
     before we decide whether it's ours; oh well */
  unlink(iorfile);

  g_free(iorfile);
  
  if (rmdir(lock->lock_directory) < 0)
    {
      gconf_set_error(err,
                      GCONF_ERROR_FAILED,
                      _("Failed to release lock directory `%s': %s"),
                      lock->lock_directory,
                      strerror(errno));
      gconf_lock_destroy(lock);
      return FALSE;
    }
  gconf_lock_destroy(lock);
  return TRUE;
}


