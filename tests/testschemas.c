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

#include <gconf/gconf.h>
#include <stdio.h>
#include <unistd.h>
#include <math.h>
#include <gconf/gconf-internals.h>

static void
check(gboolean condition, const gchar* fmt, ...)
{
  va_list args;
  gchar* description;
  
  va_start (args, fmt);
  description = g_strdup_vprintf(fmt, args);
  va_end (args);
  
  if (condition)
    {
      printf(".");
      fflush(stdout);
    }
  else
    {
      fprintf(stderr, "\n*** FAILED: %s\n", description);
      exit(1);
    }
  
  g_free(description);
}

static const gchar*
keys[] = {
  "/testing/foo/tar",
  "/testing/foo/bar",
  "/testing/quad",
  "/testing/blah",
  "/testing/q/a/b/c/z/w/x/y/z",
  "/testing/foo/baz",
  "/testing/oops/bloo",
  "/testing/oops/snoo",
  "/testing/oops/kwoo",
  "/testing/foo/quaz",
  NULL
};

static const gchar*
locales[] = {
  "C",
  "es",
  "es:en:C",
  "en:es",
  NULL
};

static gint ints[] = { -1, -2, -3, 0, 1, 2, 3, 4000, 0xfffff, -0xfffff, G_MININT, G_MAXINT, 0, 0, 57, 83, 95 };
static const guint n_ints = sizeof(ints)/sizeof(ints[0]);

static void
check_unset(GConfEngine* conf)
{
  GConfError* err = NULL;
  const gchar** keyp = NULL;

  keyp = keys;

  while (*keyp)
    {
      gconf_unset(conf, *keyp, &err);

      if (err != NULL)
        {
          fprintf(stderr, "unset of `%s' failed: %s\n", *keyp, err->str);
          gconf_error_destroy(err);
          err = NULL;
        }
      else
        {
          GConfValue* val;
          gchar* valstr;
          
          val = gconf_get(conf, *keyp, &err);


          if (val)
            valstr = gconf_value_to_string(val);
          else
            valstr = g_strdup("(none)");
          
          check(val == NULL, "unsetting a previously-set value `%s' the value `%s' existed", *keyp, valstr);

          g_free(valstr);
        }
      
      ++keyp;
    }
}

void
check_int_storage(GConfEngine* conf)
{
  GConfError* err = NULL;
  const gchar** keyp = NULL;
  guint i; 
  
  /* Loop over keys, storing all values at each then retrieving them */
  
  keyp = keys;

  while (*keyp)
    {
      i = 0;
      while (i < n_ints)
        {
          gint gotten;
          
          if (!gconf_set_int(conf, *keyp, ints[i], &err))
            {
              fprintf(stderr, "Failed to set key `%s' to `%d': %s\n",
                      *keyp, ints[i], err->str);
              gconf_error_destroy(err);
              err = NULL;
            }
          else
            {
              gotten = gconf_get_int(conf, *keyp, &err);

              if (err != NULL)
                {
                  check(gotten == 0.0, "0.0 not returned though there was an error");

                  fprintf(stderr, "Failed to get key `%s': %s\n",
                          *keyp, err->str);
                  gconf_error_destroy(err);
                  err = NULL;
                }
              else
                {
                  check (ints[i] == gotten,
                         "int set/get pair: `%d' set, `%d' got",
                         ints[i], gotten);

                }
            }
          
          ++i;
        }
      
      ++keyp;
    }

  /* Now invert the loop and see if that causes problems */

  i = 0;
  while (i < n_ints)
    {

      keyp = keys;

      while (*keyp)
        {
          gint gotten;
          
          if (!gconf_set_int(conf, *keyp, ints[i], &err))
            {
              fprintf(stderr, "Failed to set key `%s' to `%d': %s\n",
                      *keyp, ints[i], err->str);
              gconf_error_destroy(err);
              err = NULL;
            }
          else
            {
              gotten = gconf_get_int(conf, *keyp, &err);

              if (err != NULL)
                {
                  check(gotten == 0, "0 not returned though there was an error");

                  fprintf(stderr, "Failed to get key `%s': %s\n",
                          *keyp, err->str);
                  gconf_error_destroy(err);
                  err = NULL;
                }
              else
                {
                  check (ints[i] == gotten,
                         "int set/get pair: `%d' set, `%d' got",
                         ints[i], gotten);

                }
            }
          
      
          ++keyp;
        }

      ++i;
    }
          
  check_unset(conf);
}

static int
null_safe_strcmp(const char* lhs, const char* rhs)
{
  if (lhs == NULL && rhs == NULL)
    return 0;
  else if (lhs == NULL)
    return 1;
  else if (rhs == NULL)
    return -1;
  else
    return strcmp(lhs, rhs);
}

void
check_one_schema(GConfEngine* conf, const gchar** keyp, GConfSchema* schema)
{
  GConfError* err = NULL;
  
  if (!gconf_set_schema(conf, *keyp, schema, &err))
    {
      fprintf(stderr, "Failed to set key `%s' to schema: %s\n",
              *keyp, err->str);
      gconf_error_destroy(err);
      err = NULL;
    }
  else
    {
      GConfSchema* gotten;
      
      gotten = gconf_get_schema(conf, *keyp, &err);

      if (err != NULL)
        {
          check(gotten == NULL, "NULL not returned though there was an error");

          fprintf(stderr, "Failed to get key `%s': %s\n",
                  *keyp, err->str);
          gconf_error_destroy(err);
          err = NULL;
        }
      else
        {
          check (gconf_schema_type(schema) == gconf_schema_type(gotten),
                 "schema set/get pair: type `%s' set, `%s' got",
                 gconf_value_type_to_string(gconf_schema_type(schema)),
                 gconf_value_type_to_string(gconf_schema_type(gotten)));
#if 0
          /* This is wrong, the locale doesn't have to be the same */
          check (null_safe_strcmp(gconf_schema_locale(schema), gconf_schema_locale(gotten)) == 0,
                 "schema set/get pair: locale `%s' set, `%s' got",
                 gconf_schema_locale(schema),
                 gconf_schema_locale(gotten));
#endif
          
          check (null_safe_strcmp(gconf_schema_short_desc(schema), gconf_schema_short_desc(gotten)) == 0,
                 "schema set/get pair: short_desc `%s' set, `%s' got",
                 gconf_schema_short_desc(schema),
                 gconf_schema_short_desc(gotten));

          check (null_safe_strcmp(gconf_schema_long_desc(schema), gconf_schema_long_desc(gotten)) == 0,
                 "schema set/get pair: long_desc `%s' set, `%s' got",
                 gconf_schema_long_desc(schema),
                 gconf_schema_long_desc(gotten));

          check (null_safe_strcmp(gconf_schema_owner(schema), gconf_schema_owner(gotten)) == 0,
                 "schema set/get pair: owner `%s' set, `%s' got",
                 gconf_schema_owner(schema),
                 gconf_schema_owner(gotten));

          {
            GConfValue* set_default;
            GConfValue* got_default;

            set_default = gconf_schema_default_value(schema);
            got_default = gconf_schema_default_value(gotten);

            if (set_default && got_default)
              {
                check(set_default->type == GCONF_VALUE_INT,
                      "set default type is INT");
                
                check(set_default->type == got_default->type,
                      "schema set/get pair: default value type `%s' set, `%s' got",
                      gconf_value_type_to_string(set_default->type),
                      gconf_value_type_to_string(got_default->type));
                
                check(set_default->type == got_default->type,
                      "schema set/get pair: default value type `%s' set, `%s' got",
                      gconf_value_type_to_string(set_default->type),
                      gconf_value_type_to_string(got_default->type));
                
                check(gconf_value_int(set_default) == gconf_value_int(got_default),
                      "schema set/get pair: default value (int) `%d' set, `%d' got",
                      gconf_value_int(set_default), gconf_value_int(got_default));
              }
            else
              {
                /* mem leak */
                check (set_default == NULL && got_default == NULL,
                       "set and got default value aren't both NULL (`%s' and `%s')",
                       set_default ? gconf_value_to_string(set_default) : "NULL",
                       got_default ? gconf_value_to_string(got_default) : "NULL");
              }
          }
          
          gconf_schema_destroy(gotten);
        }
    }
}
      
void
check_schema_storage(GConfEngine* conf)
{
  const gchar** keyp = NULL;
  guint i; 
  const gchar** localep = NULL;
  
  /* Loop over keys, storing all values at each then retrieving them */
  
  keyp = keys;
  localep = locales;

  while (*keyp)
    {
      i = 0;
      while (i < n_ints)
        {
          GConfSchema* schema;
          gchar* short_desc;
          gchar* long_desc;
          GConfValue* default_value;
          const int default_value_int = 97992;

          if (*localep == NULL)
            localep = locales;
          
          schema = gconf_schema_new();

          gconf_schema_set_type(schema, GCONF_VALUE_INT);
          gconf_schema_set_locale(schema, *localep);
          short_desc = g_strdup_printf("Schema for key `%s' storing value %d",
                                       *keyp, ints[i]);
          gconf_schema_set_short_desc(schema, short_desc);

          long_desc = g_strdup_printf("Long description for schema with short description `%s' is really really long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long ", short_desc);
          
          gconf_schema_set_long_desc(schema, long_desc);

          gconf_schema_set_owner(schema, "testschemas");

          default_value = gconf_value_new(GCONF_VALUE_INT);
          gconf_value_set_int(default_value, default_value_int);
          
          gconf_schema_set_default_value_nocopy(schema, default_value);

          check(gconf_value_int(gconf_schema_default_value(schema)) == default_value_int,
                "Properly stored default int value in the schema");
          
          check_one_schema(conf, keyp, schema);

          gconf_schema_destroy(schema);
          g_free(long_desc);
          g_free(short_desc);
          
          ++i;
        }
      
      ++keyp;
      ++localep;
    }

  /* Check setting/getting "empty" schemas */
  
  keyp = keys;

  while (*keyp)
    {
      i = 0;
      while (i < n_ints)
        {
          GConfSchema* schema;
          
          schema = gconf_schema_new();

          /* this isn't guaranteed to be the same on get/set */
          gconf_schema_set_locale(schema, "C");
          
          gconf_schema_set_type(schema, GCONF_VALUE_INT);

          check_one_schema(conf, keyp, schema);

          gconf_schema_destroy(schema);
          
          ++i;
        }
      
      ++keyp;
    }
  
  check_unset(conf);
}



int 
main (int argc, char** argv)
{
  GConfEngine* conf;
  GConfError* err = NULL;
  
  if (!gconf_init(argc, argv, &err))
    {
      fprintf(stderr, "Failed to init GConf: %s\n", err->str);
      gconf_error_destroy(err);
      err = NULL;
      return 1;
    }
  
  conf = gconf_engine_new();

  check(conf != NULL, "create the default conf engine");
  
  printf("\nChecking integer storage:");
  
  check_int_storage(conf);

  printf("\nChecking schema storage:");

  check_schema_storage(conf);
  
  gconf_engine_unref(conf);

  printf("\n\n");
  
  return 0;
}
